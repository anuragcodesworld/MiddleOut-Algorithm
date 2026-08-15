#include "../include/moc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/*
 * ============================================================
 * MOC V7.3
 * ============================================================
 *
 * V7.3 keeps the MOC5 file format and MOC3/MOC5 decoder.
 *
 * Main improvement over V7.2:
 *
 *   V7.2:
 *       prev[input_size]
 *       -> memory grows 4 bytes per input byte
 *
 *   V7.3:
 *       fixed-size hash buckets
 *       -> memory is independent of input size
 *
 * Features:
 *
 *   - MOC5 compatible output
 *   - MOC3/MOC5 compatible decoder
 *   - variable-length integers
 *   - cost-aware references
 *   - 3-byte hash index
 *   - bounded-memory match index
 *   - maximum 64 candidates per position
 *   - overlapping matches supported
 *
 * ============================================================
 */

#define MOC_LITERAL_BLOCK 0
#define MOC_REFERENCE     1

#define MIN_MATCH 3
#define MAX_MATCH 256

#define MAX_CANDIDATES 64

#define HASH_SIZE 65536

/*
 * Number of recent positions stored per hash bucket.
 *
 * Memory:
 *
 *     HASH_SIZE * CHAIN_LIMIT * sizeof(uint32_t)
 *
 *     65536 * 8 * 4
 *     = 2 MB
 *
 * This remains constant regardless of input size.
 */
#define CHAIN_LIMIT 8

#define INVALID_POSITION UINT32_MAX


/*
 * ============================================================
 * VARINT
 * ============================================================
 */

static size_t varint_size(uint32_t value)
{
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
)
{
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
)
{
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
 */

static uint32_t hash3(
    const unsigned char *data
)
{
    uint32_t value =
        ((uint32_t)data[0] << 16) |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2]);


    value ^= value >> 11;

    value *= 2654435761u;

    value ^= value >> 16;


    return value & (HASH_SIZE - 1);
}


/*
 * ============================================================
 * REFERENCE COST
 * ============================================================
 */

static size_t reference_cost(
    size_t offset,
    size_t length
)
{
    return
        1 +
        varint_size((uint32_t)offset) +
        varint_size((uint32_t)length);
}


/*
 * ============================================================
 * BOUNDED HASH INDEX
 * ============================================================
 *
 * Each hash bucket contains only CHAIN_LIMIT recent positions.
 *
 * This prevents memory usage from growing with input size.
 *
 * Layout:
 *
 *     chains[hash][0] = newest
 *     chains[hash][1] = next newest
 *     ...
 *
 * ============================================================
 */

typedef struct HashIndex {

    uint32_t *positions;

    unsigned char *counts;

} HashIndex;


/*
 * ============================================================
 * INITIALIZE INDEX
 * ============================================================
 */

static int index_init(
    HashIndex *index
)
{
    size_t position_count =
        HASH_SIZE * CHAIN_LIMIT;


    index->positions =
        malloc(
            position_count *
            sizeof(uint32_t)
        );


    if (!index->positions) {
        return 0;
    }


    index->counts =
        calloc(
            HASH_SIZE,
            sizeof(unsigned char)
        );


    if (!index->counts) {

        free(index->positions);

        index->positions = NULL;

        return 0;
    }


    for (
        size_t i = 0;
        i < position_count;
        i++
    ) {

        index->positions[i] =
            INVALID_POSITION;
    }


    return 1;
}


/*
 * ============================================================
 * FREE INDEX
 * ============================================================
 */

static void index_free(
    HashIndex *index
)
{
    free(index->positions);

    free(index->counts);

    index->positions = NULL;

    index->counts = NULL;
}


/*
 * ============================================================
 * ADD POSITION
 * ============================================================
 */

static int index_position(
    const unsigned char *data,
    size_t size,
    size_t position,
    HashIndex *index
)
{
    if (
        position + MIN_MATCH >
        size
    ) {
        return 1;
    }


    if (
        position >
        UINT32_MAX
    ) {
        return 0;
    }


    uint32_t hash =
        hash3(data + position);


    size_t base =
        (size_t)hash *
        CHAIN_LIMIT;


    unsigned int count =
        index->counts[hash];


    /*
     * Shift older entries toward the end.
     */
    if (count < CHAIN_LIMIT) {

        for (
            size_t i = count;
            i > 0;
            i--
        ) {

            index->positions[base + i] =
                index->positions[base + i - 1];
        }

        index->counts[hash] =
            (unsigned char)(count + 1);

    } else {

        for (
            size_t i = CHAIN_LIMIT - 1;
            i > 0;
            i--
        ) {

            index->positions[base + i] =
                index->positions[base + i - 1];
        }
    }


    index->positions[base] =
        (uint32_t)position;


    return 1;
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
    const HashIndex *index,
    size_t *best_offset,
    size_t *best_length,
    int *best_score
)
{
    *best_offset = 0;

    *best_length = 0;

    *best_score = -1000000;


    if (
        position + MIN_MATCH >
        size
    ) {
        return;
    }


    uint32_t hash =
        hash3(data + position);


    size_t base =
        (size_t)hash *
        CHAIN_LIMIT;


    unsigned int count =
        index->counts[hash];


    size_t candidates = 0;


    /*
     * Candidates are newest to oldest.
     */
    for (
        unsigned int i = 0;
        i < count &&
        candidates < MAX_CANDIDATES;
        i++
    ) {

        uint32_t previous32 =
            index->positions[base + i];


        if (
            previous32 ==
            INVALID_POSITION
        ) {
            continue;
        }


        size_t previous =
            (size_t)previous32;


        if (
            previous >= position
        ) {
            continue;
        }


        size_t offset =
            position - previous;


        if (offset == 0) {
            continue;
        }


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
         * ----------------------------------------------------
         * Match calculation
         * ----------------------------------------------------
         *
         * Support overlapping LZ-style matches.
         *
         * For offset >= length, the source bytes are directly
         * available in the already processed input.
         *
         * For offset < length, the decoder repeats the pattern.
         *
         * Therefore:
         *
         *     data[position + length]
         *
         * must equal:
         *
         *     data[previous + (length % offset)]
         *
         * ----------------------------------------------------
         */

        while (
            length < maximum_length
        ) {

            size_t source =
                previous +
                (length % offset);


            /*
             * For compression input, source may refer to bytes
             * that are logically produced by overlap.
             *
             * We only have the original input available here.
             *
             * For source >= position, use the corresponding
             * already-existing input byte through the periodic
             * pattern.
             */
            if (
                source >= position
            ) {

                size_t repeated =
                    previous +
                    (source - position);

                if (
                    repeated >=
                    position
                ) {
                    repeated =
                        previous +
                        (
                            (length % offset)
                        );
                }

                source =
                    repeated;
            }


            if (
                source >= size
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


        if (
            length >= MIN_MATCH
        ) {

            size_t cost =
                reference_cost(
                    offset,
                    length
                );


            int score =
                (int)length -
                (int)cost;


            if (
                score > *best_score ||
                (
                    score == *best_score &&
                    length > *best_length
                ) ||
                (
                    score == *best_score &&
                    length == *best_length &&
                    (
                        *best_offset == 0 ||
                        offset < *best_offset
                    )
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
    }
}


/*
 * ============================================================
 * COMPRESSOR
 * ============================================================
 */

int moc_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
)
{
    /*
     * MOC5 header stores original size in uint32_t.
     */
    if (
        input_size >
        UINT32_MAX
    ) {
        return 0;
    }


    /*
     * Prevent integer overflow when allocating output.
     */
    if (
        input_size >
        (SIZE_MAX - 1024) / 2
    ) {
        return 0;
    }


    size_t capacity =
        input_size * 2 + 1024;


    unsigned char *buffer =
        malloc(capacity);


    if (!buffer) {
        return 0;
    }


    /*
     * Initialize bounded hash index.
     */
    HashIndex index;


    if (
        !index_init(&index)
    ) {

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


    /*
     * ========================================================
     * MAIN COMPRESSION LOOP
     * ========================================================
     */

    while (
        position < input_size
    ) {

        size_t offset = 0;

        size_t length = 0;

        int score = -1000000;


        find_match_indexed(
            input,
            input_size,
            position,
            &index,
            &offset,
            &length,
            &score
        );


        /*
         * ----------------------------------------------------
         * REFERENCE
         * ----------------------------------------------------
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
             * Index all consumed positions.
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
                        &index
                    )
                ) {

                    index_free(&index);

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
                &index,
                &test_offset,
                &test_length,
                &test_score
            );


            /*
             * Stop the literal block as soon as a profitable
             * reference is available.
             */
            if (
                test_score > 0 &&
                test_length >= MIN_MATCH
            ) {
                break;
            }


            if (
                !index_position(
                    input,
                    input_size,
                    position,
                    &index
                )
            ) {

                index_free(&index);

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


    index_free(&index);


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
 * Supports:
 *
 *     MOC3
 *     MOC5
 *
 * Kept compatible with V6/V7/V7.2.
 *
 * ============================================================
 */

int moc_decompress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
)
{
    if (
        input_size < 8
    ) {
        return 0;
    }


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
     * Final integrity check.
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
