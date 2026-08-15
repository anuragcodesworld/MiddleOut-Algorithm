#include "../include/moc.h"

#include <stdint.h>
#include <stdlib.h>


/*
 * ============================================================
 * MOC V5
 * ============================================================
 *
 * V5 introduces:
 *
 *   1. Variable-length integers (varints)
 *   2. Cost-aware reference selection
 *   3. MOC5 file format
 *
 * The old MOC3 format is still supported by the decoder.
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
#define MAX_MATCH 32


/*
 * ============================================================
 * VARINT
 * ============================================================
 *
 * Small integers use fewer bytes.
 *
 * Examples:
 *
 *     5      -> 1 byte
 *     127    -> 1 byte
 *     128    -> 2 bytes
 *     300    -> 2 bytes
 *
 * The high bit indicates whether another byte follows.
 */


/*
 * Return number of bytes needed for a varint.
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


/*
 * Write a varint.
 */
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


/*
 * Read a varint.
 */
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


        /*
         * High bit clear = final byte.
         */
        if ((byte & 0x80) == 0) {

            *value = result;

            return 1;
        }


        shift += 7;


        /*
         * Prevent malformed input
         * from shifting indefinitely.
         */
        if (shift >= 32) {
            return 0;
        }
    }


    return 0;
}


/*
 * ============================================================
 * MIDDLE-OUT MATCH SEARCH
 * ============================================================
 *
 * Searches the already processed portion of the input.
 *
 * The search starts around the middle of the available
 * history and expands outward.
 *
 * V5 returns the best candidate according to:
 *
 *     match length - encoded reference cost
 *
 * This makes the search aware of the actual MOC5 format.
 */


/*
 * Calculate the cost of a reference.
 *
 * Format:
 *
 *     1 byte token
 *     varint offset
 *     varint length
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
 * Find best Middle-Out match.
 */
static void find_match_middle_out(
    const unsigned char *data,
    size_t size,
    size_t position,
    size_t *best_offset,
    size_t *best_length,
    int *best_score
) {
    *best_offset = 0;

    *best_length = 0;

    *best_score = -1000000;


    /*
     * Not enough history.
     */
    if (position < MIN_MATCH) {
        return;
    }


    /*
     * Maximum possible match.
     */
    size_t maximum_length =
        size - position;


    if (maximum_length > MAX_MATCH) {
        maximum_length = MAX_MATCH;
    }


    /*
     * Search history.
     */
    size_t history = position;


    /*
     * Middle point of history.
     */
    size_t middle =
        history / 2;


    /*
     * Expand from the middle outward.
     */
    for (
        size_t distance = 0;
        distance < history;
        distance++
    ) {

        size_t candidates[2];

        size_t candidate_count = 0;


        /*
         * Left candidate.
         */
        if (middle >= distance) {

            candidates[candidate_count++] =
                middle - distance;
        }


        /*
         * Right candidate.
         */
        if (
            distance != 0 &&
            middle + distance < history
        ) {

            candidates[candidate_count++] =
                middle + distance;
        }


        /*
         * Test candidates.
         */
        for (
            size_t c = 0;
            c < candidate_count;
            c++
        ) {

            size_t previous =
                candidates[c];


            /*
             * Candidate must be before
             * current position.
             */
            if (previous >= position) {
                continue;
            }


            size_t offset =
                position - previous;


            if (offset == 0) {
                continue;
            }


            size_t length = 0;


            /*
             * Compare bytes.
             *
             * Overlapping matches are allowed.
             */
            while (
                length < maximum_length
            ) {

                size_t source =
                    previous +
                    (length % offset);


                size_t target =
                    position + length;


                if (source >= position) {
                    break;
                }


                if (
                    data[source] !=
                    data[target]
                ) {
                    break;
                }


                length++;
            }


            if (length < MIN_MATCH) {
                continue;
            }


            /*
             * Calculate actual encoded cost.
             */
            size_t cost =
                reference_cost(
                    offset,
                    length
                );


            /*
             * How many bytes would be
             * represented by the match?
             */
            int score =
                (int)length -
                (int)cost;


            /*
             * Prefer:
             *
             * 1. Higher savings
             * 2. Longer match if savings equal
             * 3. Smaller offset if everything else equal
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

                *best_score = score;

                *best_length = length;

                *best_offset = offset;
            }
        }
    }
}


/*
 * ============================================================
 * MOC V5 COMPRESSOR
 * ============================================================
 */
int moc_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
) {
    /*
     * Prototype allocation.
     *
     * Worst case is slightly larger than input because
     * literal blocks have headers.
     */
    size_t capacity =
        input_size * 2 + 1024;


    unsigned char *buffer =
        malloc(capacity);


    if (!buffer) {
        return 0;
    }


    size_t out = 0;


    /*
     * --------------------------------------------------------
     * MOC5 HEADER
     * --------------------------------------------------------
     *
     * 4 bytes magic
     * 4 bytes original size
     */
    buffer[out++] = 'M';
    buffer[out++] = 'O';
    buffer[out++] = 'C';
    buffer[out++] = '5';


    /*
     * Original size is stored as little-endian uint32.
     */
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


    while (position < input_size) {

        size_t offset = 0;

        size_t length = 0;

        int score = -1000000;


        /*
         * Find best reference.
         */
        find_match_middle_out(
            input,
            input_size,
            position,
            &offset,
            &length,
            &score
        );


        /*
         * If reference saves bytes, use it.
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


            position += length;

            continue;
        }


        /*
         * ----------------------------------------------------
         * LITERAL BLOCK
         * ----------------------------------------------------
         *
         * We collect consecutive bytes until a profitable
         * reference appears.
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


            find_match_middle_out(
                input,
                input_size,
                position,
                &test_offset,
                &test_length,
                &test_score
            );


            /*
             * A profitable reference starts here.
             */
            if (
                test_score > 0 &&
                test_length >= MIN_MATCH
            ) {
                break;
            }


            position++;

            literal_length++;
        }


        /*
         * Literal block:
         *
         * token
         * varint length
         * raw bytes
         */
        buffer[out++] =
            MOC_LITERAL_BLOCK;


        write_varint(
            buffer,
            &out,
            (uint32_t)literal_length
        );


        for (
            size_t i = 0;
            i < literal_length;
            i++
        ) {

            buffer[out++] =
                input[
                    literal_start + i
                ];
        }
    }


    *output = buffer;

    *output_size = out;

    return 1;
}


/*
 * ============================================================
 * MOC V5 DECOMPRESSOR
 * ============================================================
 *
 * Supports:
 *
 *     MOC3
 *     MOC5
 *
 * This allows old compressed files to remain readable.
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
     * Detect format version.
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


    if (!is_moc3 && !is_moc5) {
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
         * ====================================================
         * LITERAL BLOCK
         * ====================================================
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

                /*
                 * MOC3 fixed 4-byte length.
                 */
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
                input_size
            ) {

                free(buffer);

                return 0;
            }


            if (
                out + length >
                original_size
            ) {

                free(buffer);

                return 0;
            }


            for (
                uint32_t i = 0;
                i < length;
                i++
            ) {

                buffer[out++] =
                    input[position++];
            }
        }


        /*
         * ====================================================
         * REFERENCE BLOCK
         * ====================================================
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

                /*
                 * MOC3 fixed-width reference.
                 */
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


            /*
             * Validate reference.
             */
            if (
                offset == 0 ||
                offset > out
            ) {

                free(buffer);

                return 0;
            }


            if (
                out + length >
                original_size
            ) {

                free(buffer);

                return 0;
            }


            size_t source =
                out - offset;


            /*
             * Copy reference.
             *
             * Overlapping copies are allowed.
             */
            for (
                uint32_t i = 0;
                i < length;
                i++
            ) {

                buffer[out++] =
                    buffer[
                        source + i
                    ];
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


    *output = buffer;

    *output_size = out;

    return 1;
}
