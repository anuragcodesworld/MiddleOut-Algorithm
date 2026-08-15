#include "../include/moc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/*
 * ============================================================
 * MOC V6
 * ============================================================
 *
 * V6 keeps the MOC5 file format but replaces the expensive
 * brute-force match search with a 3-byte hash index.
 *
 * Features:
 *
 *   - MOC5 compatible output
 *   - MOC3 compatible decoder
 *   - variable-length integers
 *   - cost-aware references
 *   - indexed match search
 *
 */


/*
 * Block types
 */
#define MOC_LITERAL_BLOCK   0
#define MOC_REFERENCE       1


/*
 * Matching parameters
 */
#define MIN_MATCH 3
#define MAX_MATCH 256


/*
 * Maximum number of candidates examined for one position.
 *
 * This prevents pathological inputs such as:
 *
 *     AAAAAAAAAAAAAAAAAAAAA...
 *
 * from causing enormous search times.
 */
#define MAX_CANDIDATES 64


/*
 * Hash table size.
 *
 * Power of two allows fast modulo using &.
 */
#define HASH_SIZE 65536


/*
 * ============================================================
 * VARINT
 * ============================================================
 */

static size_t varint_size(
    uint32_t value
) {
    size_t size = 1;

    while (value >= 128) {
        value >>= 7;
        size++;
    }

    return size;
}


static void write_varint(
    unsigned char *buffer,
    size_t *position,
    uint32_t value
) {
    while (value >= 128) {

        buffer[(*position)++] =
            (unsigned char)(
                (value & 0x7F) | 0x80
            );

        value >>= 7;
    }

    buffer[(*position)++] =
        (unsigned char)value;
}


static int read_varint(
    const unsigned char *buffer,
    size_t input_size,
    size_t *position,
    uint32_t *value
) {
    uint32_t result = 0;

    unsigned int shift = 0;


    while (*position < input_size) {

        unsigned char byte =
            buffer[(*position)++];


        result |=
            (uint32_t)(byte & 0x7F)
            << shift;


        if ((byte & 0x80) == 0) {

            *value = result;

            return 1;
        }


        shift += 7;


        if (shift >= 32) {
            return 0;
        }
    }


    return 0;
}


/*
 * ============================================================
 * HASH
 * ============================================================
 *
 * Hash exactly three bytes.
 */
static uint32_t hash3(
    const unsigned char *data
) {
    uint32_t value =
        ((uint32_t)data[0] << 16) |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2]);


    /*
     * Mix the bits.
     */
    value ^= value >> 11;

    value *= 2654435761u;

    value ^= value >> 16;


    return value & (HASH_SIZE - 1);
}


/*
 * ============================================================
 * MATCH INDEX
 * ============================================================
 *
 * Each hash bucket is a linked list of previous positions.
 */

typedef struct MatchNode {

    size_t position;

    struct MatchNode *next;

} MatchNode;


/*
 * ============================================================
 * REFERENCE COST
 * ============================================================
 */

static size_t reference_cost(
    size_t offset,
    size_t length
) {
    return
        1 +
        varint_size((uint32_t)offset) +
        varint_size((uint32_t)length);
}


/*
 * ============================================================
 * FIND BEST MATCH
 * ============================================================
 */

static void find_match_indexed(
    const unsigned char *data,
    size_t size,
    size_t position,
    MatchNode **table,
    size_t *best_offset,
    size_t *best_length,
    int *best_score
) {
    *best_offset = 0;

    *best_length = 0;

    *best_score = -1000000;


    /*
     * Need at least three bytes for the index key.
     */
    if (
        position + MIN_MATCH >
        size
    ) {
        return;
    }


    /*
     * Look up current three-byte sequence.
     */
    uint32_t hash =
        hash3(data + position);


    MatchNode *node =
        table[hash];


    size_t candidates = 0;


    /*
     * Examine candidates from newest to oldest.
     */
    while (
        node != NULL &&
        candidates < MAX_CANDIDATES
    ) {

        size_t previous =
            node->position;


        /*
         * Safety.
         */
        if (previous >= position) {
            node = node->next;
            continue;
        }


        size_t offset =
            position - previous;


        /*
         * Find match length.
         */
        size_t maximum_length =
            size - position;


        if (
            maximum_length >
            MAX_MATCH
        ) {
            maximum_length =
                MAX_MATCH;
        }


        size_t length = 0;


        /*
         * Overlapping matches are supported.
         */
        while (
            length < maximum_length
        ) {

            size_t source =
                previous +
                (length % offset);


            if (
                source >= position
            ) {
                break;
            }


            if (
                data[source] !=
                data[position + length]
            ) {
                break;
            }


            length++;
        }


        if (length >= MIN_MATCH) {

            size_t cost =
                reference_cost(
                    offset,
                    length
                );


            int score =
                (int)length -
                (int)cost;


            /*
             * Prefer maximum savings.
             */
            if (
                score > *best_score ||
                (
                    score == *best_score &&
                    length > *best_length
                ) ||
                (
                    score == *best_score &&
                    length == *best_length &&
                    offset < *best_offset
                )
            ) {

                *best_score =
                    score;

                *best_length =
                    length;

                *best_offset =
                    offset;
            }
        }


        candidates++;

        node = node->next;
    }
}


/*
 * ============================================================
 * ADD POSITION TO INDEX
 * ============================================================
 */

static int index_position(
    const unsigned char *data,
    size_t size,
    size_t position,
    MatchNode **table
) {
    if (
        position + MIN_MATCH >
        size
    ) {
        return 1;
    }


    uint32_t hash =
        hash3(data + position);


    MatchNode *node =
        malloc(sizeof(MatchNode));


    if (!node) {
        return 0;
    }


    node->position =
        position;


    node->next =
        table[hash];


    table[hash] =
        node;


    return 1;
}


/*
 * ============================================================
 * FREE INDEX
 * ============================================================
 */

static void free_index(
    MatchNode **table
) {
    for (
        size_t i = 0;
        i < HASH_SIZE;
        i++
    ) {

        MatchNode *node =
            table[i];


        while (node) {

            MatchNode *next =
                node->next;


            free(node);

            node = next;
        }
    }
}


/*
 * ============================================================
 * MOC5 COMPRESSOR
 * ============================================================
 */

int moc_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
) {
    /*
     * Allocate output buffer.
     */
    size_t capacity =
        input_size * 2 + 1024;


    unsigned char *buffer =
        malloc(capacity);


    if (!buffer) {
        return 0;
    }


    /*
     * Allocate hash table.
     */
    MatchNode **table =
        calloc(
            HASH_SIZE,
            sizeof(MatchNode *)
        );


    if (!table) {

        free(buffer);

        return 0;
    }


    size_t out = 0;


    /*
     * --------------------------------------------------------
     * MOC5 HEADER
     * --------------------------------------------------------
     */
    buffer[out++] = 'M';
    buffer[out++] = 'O';
    buffer[out++] = 'C';
    buffer[out++] = '5';


    uint32_t original_size =
        (uint32_t)input_size;


    buffer[out++] =
        (unsigned char)(
            original_size & 0xFF
        );


    buffer[out++] =
        (unsigned char)(
            (original_size >> 8) & 0xFF
        );


    buffer[out++] =
        (unsigned char)(
            (original_size >> 16) & 0xFF
        );


    buffer[out++] =
        (unsigned char)(
            (original_size >> 24) & 0xFF
        );


    size_t position = 0;


    while (
        position < input_size
    ) {

        size_t offset = 0;

        size_t length = 0;

        int score = -1000000;


        /*
         * Find best indexed match.
         */
        find_match_indexed(
            input,
            input_size,
            position,
            table,
            &offset,
            &length,
            &score
        );


        /*
         * Use reference if it saves space.
         */
        if (
            score > 0 &&
            length >= MIN_MATCH
        ) {

            buffer[out++] =
                MOC_REFERENCE;


            write_varint(
                buffer,
                &out,
                (uint32_t)offset
            );


            write_varint(
                buffer,
                &out,
                (uint32_t)length
            );


            /*
             * Add every consumed position
             * to the index.
             */
            for (
                size_t i = 0;
                i < length;
                i++
            ) {

                if (
                    !index_position(
                        input,
                        input_size,
                        position + i,
                        table
                    )
                ) {

                    free_index(table);

                    free(table);

                    free(buffer);

                    return 0;
                }
            }


            position += length;

            continue;
        }


        /*
         * ----------------------------------------------------
         * LITERAL BLOCK
         * ----------------------------------------------------
         *
         * Gather bytes until a profitable reference
         * appears.
         */
        size_t literal_start =
            position;


        size_t literal_length = 0;


        while (
            position < input_size
        ) {

            size_t test_offset = 0;

            size_t test_length = 0;

            int test_score = -1000000;


            find_match_indexed(
                input,
                input_size,
                position,
                table,
                &test_offset,
                &test_length,
                &test_score
            );


            if (
                test_score > 0 &&
                test_length >= MIN_MATCH
            ) {
                break;
            }


            /*
             * Add this literal position to index.
             */
            if (
                !index_position(
                    input,
                    input_size,
                    position,
                    table
                )
            ) {

                free_index(table);

                free(table);

                free(buffer);

                return 0;
            }


            position++;

            literal_length++;
        }


        /*
         * Literal token.
         */
        buffer[out++] =
            MOC_LITERAL_BLOCK;


        write_varint(
            buffer,
            &out,
            (uint32_t)literal_length
        );


        memcpy(
            buffer + out,
            input + literal_start,
            literal_length
        );


        out += literal_length;
    }


    free_index(table);

    free(table);


    *output =
        buffer;


    *output_size =
        out;


    return 1;
}


/*
 * ============================================================
 * DECOMPRESSOR
 * ============================================================
 *
 * Supports both MOC3 and MOC5.
 */
int moc_decompress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
) {
    if (input_size < 8) {
        return 0;
    }


    /*
     * Detect format.
     */
    int is_moc3 =
        input[0] == 'M' &&
        input[1] == 'O' &&
        input[2] == 'C' &&
        input[3] == '3';


    int is_moc5 =
        input[0] == 'M' &&
        input[1] == 'O' &&
        input[2] == 'C' &&
        input[3] == '5';


    if (
        !is_moc3 &&
        !is_moc5
    ) {
        return 0;
    }


    size_t position = 4;


    /*
     * Original size.
     */
    uint32_t original_size =
        (uint32_t)input[position++];

    original_size |=
        (uint32_t)input[position++] << 8;

    original_size |=
        (uint32_t)input[position++] << 16;

    original_size |=
        (uint32_t)input[position++] << 24;


    unsigned char *buffer =
        malloc(original_size);


    if (
        !buffer &&
        original_size > 0
    ) {
        return 0;
    }


    size_t out = 0;


    while (
        position < input_size &&
        out < original_size
    ) {

        unsigned char type =
            input[position++];


        /*
         * ----------------------------------------------------
         * LITERAL
         * ----------------------------------------------------
         */
        if (
            type ==
            MOC_LITERAL_BLOCK
        ) {

            uint32_t length = 0;


            if (is_moc5) {

                if (
                    !read_varint(
                        input,
                        input_size,
                        &position,
                        &length
                    )
                ) {

                    free(buffer);

                    return 0;
                }

            } else {

                if (
                    position + 4 >
                    input_size
                ) {

                    free(buffer);

                    return 0;
                }


                length =
                    (uint32_t)input[position++];

                length |=
                    (uint32_t)input[position++] << 8;

                length |=
                    (uint32_t)input[position++] << 16;

                length |=
                    (uint32_t)input[position++] << 24;
            }


            if (
                position + length >
                input_size ||
                out + length >
                original_size
            ) {

                free(buffer);

                return 0;
            }


            memcpy(
                buffer + out,
                input + position,
                length
            );


            position += length;

            out += length;
        }


        /*
         * ----------------------------------------------------
         * REFERENCE
         * ----------------------------------------------------
         */
        else if (
            type ==
            MOC_REFERENCE
        ) {

            uint32_t offset = 0;

            uint32_t length = 0;


            if (is_moc5) {

                if (
                    !read_varint(
                        input,
                        input_size,
                        &position,
                        &offset
                    )
                ) {

                    free(buffer);

                    return 0;
                }


                if (
                    !read_varint(
                        input,
                        input_size,
                        &position,
                        &length
                    )
                ) {

                    free(buffer);

                    return 0;
                }

            } else {

                if (
                    position + 8 >
                    input_size
                ) {

                    free(buffer);

                    return 0;
                }


                offset =
                    (uint32_t)input[position++];

                offset |=
                    (uint32_t)input[position++] << 8;

                offset |=
                    (uint32_t)input[position++] << 16;

                offset |=
                    (uint32_t)input[position++] << 24;


                length =
                    (uint32_t)input[position++];

                length |=
                    (uint32_t)input[position++] << 8;

                length |=
                    (uint32_t)input[position++] << 16;

                length |=
                    (uint32_t)input[position++] << 24;
            }


            if (
                offset == 0 ||
                offset > out ||
                out + length >
                original_size
            ) {

                free(buffer);

                return 0;
            }


            size_t source =
                out - offset;


            /*
             * Overlapping copy.
             */
            for (
                uint32_t i = 0;
                i < length;
                i++
            ) {

                buffer[out++] =
                    buffer[source + i];
            }
        }


        else {

            free(buffer);

            return 0;
        }
    }


    /*
     * Integrity check.
     */
    if (
        out != original_size
    ) {

        free(buffer);

        return 0;
    }


    *output =
        buffer;


    *output_size =
        out;


    return 1;
}
