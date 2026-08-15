#include "../include/moc.h"
#include "../include/reference.h"

#include <stdint.h>
#include <stdlib.h>


#define MOC_LITERAL_BLOCK 0
#define MOC_REFERENCE     1

#define MIN_MATCH 3
#define MAX_MATCH 32


/*
 * Write a 32-bit unsigned integer.
 */
static void write_u32(
    unsigned char *buffer,
    size_t *position,
    uint32_t value
) {
    buffer[(*position)++] =
        (unsigned char)(value & 0xFF);

    buffer[(*position)++] =
        (unsigned char)((value >> 8) & 0xFF);

    buffer[(*position)++] =
        (unsigned char)((value >> 16) & 0xFF);

    buffer[(*position)++] =
        (unsigned char)((value >> 24) & 0xFF);
}


/*
 * Read a 32-bit unsigned integer.
 */
static uint32_t read_u32(
    const unsigned char *buffer,
    size_t *position
) {
    uint32_t value = 0;

    value |=
        (uint32_t)buffer[(*position)++];

    value |=
        (uint32_t)buffer[(*position)++] << 8;

    value |=
        (uint32_t)buffer[(*position)++] << 16;

    value |=
        (uint32_t)buffer[(*position)++] << 24;

    return value;
}


/*
 * =========================================================
 * FIND MATCH - MIDDLE-OUT SEARCH
 * =========================================================
 *
 * We still encode the file from left to right.
 *
 * Middle-Out is used only to determine the order in which
 * previous positions are searched.
 *
 * References ALWAYS point backwards.
 */
static void find_match_middle_out(
    const unsigned char *data,
    size_t size,
    size_t position,
    size_t *best_offset,
    size_t *best_length
) {
    *best_offset = 0;
    *best_length = 0;


    if (position < MIN_MATCH) {
        return;
    }


    size_t maximum_length =
        size - position;


    if (maximum_length > MAX_MATCH) {
        maximum_length = MAX_MATCH;
    }


    /*
     * All data before the current position is our
     * search history.
     */
    size_t history = position;

    size_t middle = history / 2;


    /*
     * Search from the middle outward.
     */
    for (
        size_t distance = 0;
        distance < history;
        distance++
    ) {

        size_t candidates[2];

        size_t candidate_count = 0;


        /*
         * Left side.
         */
        if (middle >= distance) {

            candidates[candidate_count++] =
                middle - distance;
        }


        /*
         * Right side.
         */
        if (
            distance != 0 &&
            middle + distance < history
        ) {

            candidates[candidate_count++] =
                middle + distance;
        }


        for (
            size_t c = 0;
            c < candidate_count;
            c++
        ) {

            size_t previous =
                candidates[c];


            /*
             * Candidate must be before current
             * position.
             */
            if (previous >= position) {
                continue;
            }


            size_t distance_back =
                position - previous;


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
                    (length % distance_back);


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


            /*
             * Keep the longest match.
             */
            if (
                length >= MIN_MATCH &&
                length > *best_length
            ) {

                *best_length = length;

                *best_offset =
                    distance_back;
            }
        }
    }
}


/*
 * =========================================================
 * MOC V3 COMPRESSOR
 * =========================================================
 */
int moc_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
) {
    /*
     * Allocate generously for now.
     *
     * We will optimize the allocation later.
     */
    size_t capacity =
        input_size * 10 + 16;


    unsigned char *buffer =
        malloc(capacity);


    if (!buffer) {
        return 0;
    }


    size_t out = 0;


    /*
     * Header:
     *
     * 4 bytes magic
     * 4 bytes original size
     */
    buffer[out++] = 'M';
    buffer[out++] = 'O';
    buffer[out++] = 'C';
    buffer[out++] = '3';


    write_u32(
        buffer,
        &out,
        (uint32_t)input_size
    );


    size_t position = 0;


    while (position < input_size) {

        size_t offset = 0;
        size_t length = 0;


        /*
         * Look for a Middle-Out match.
         */
        find_match_middle_out(
            input,
            input_size,
            position,
            &offset,
            &length
        );


        /*
         * A reference costs:
         *
         * 1 byte token
         * 4 bytes offset
         * 4 bytes length
         *
         * Total = 9 bytes.
         *
         * Therefore a match of 9 bytes or less
         * is not worthwhile.
         */
        if (length > 9) {

            buffer[out++] =
                MOC_REFERENCE;


            write_u32(
                buffer,
                &out,
                (uint32_t)offset
            );


            write_u32(
                buffer,
                &out,
                (uint32_t)length
            );


            position += length;

            continue;
        }


        /*
         * =================================================
         * LITERAL BLOCK
         * =================================================
         *
         * Instead of storing:
         *
         *   TOKEN A
         *   TOKEN B
         *   TOKEN C
         *
         * we store:
         *
         *   LITERAL_BLOCK
         *   LENGTH
         *   A B C
         *
         * This is particularly important for random data.
         */
        size_t literal_start =
            position;


        size_t literal_length =
            0;


        while (position < input_size) {

            size_t test_offset = 0;
            size_t test_length = 0;


            find_match_middle_out(
                input,
                input_size,
                position,
                &test_offset,
                &test_length
            );


            /*
             * Stop the literal block when a useful
             * reference is found.
             */
            if (test_length > 9) {
                break;
            }


            position++;

            literal_length++;
        }


        /*
         * Literal block format:
         *
         * 1 byte  token
         * 4 bytes length
         * N bytes data
         */
        buffer[out++] =
            MOC_LITERAL_BLOCK;


        write_u32(
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
 * =========================================================
 * MOC V3 DECOMPRESSOR
 * =========================================================
 */
int moc_decompress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
) {
    /*
     * Minimum valid file:
     *
     * 4 bytes magic
     * 4 bytes original size
     */
    if (input_size < 8) {
        return 0;
    }


    /*
     * Check magic.
     */
    if (
        input[0] != 'M' ||
        input[1] != 'O' ||
        input[2] != 'C' ||
        input[3] != '3'
    ) {
        return 0;
    }


    size_t position = 4;


    uint32_t original_size =
        read_u32(
            input,
            &position
        );


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
         * =================================================
         * LITERAL BLOCK
         * =================================================
         */
        if (
            type ==
            MOC_LITERAL_BLOCK
        ) {

            /*
             * Need 4 bytes for length.
             */
            if (
                position + 4 >
                input_size
            ) {

                free(buffer);
                return 0;
            }


            uint32_t length =
                read_u32(
                    input,
                    &position
                );


            /*
             * Validate the block.
             */
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


            /*
             * Copy literal bytes.
             */
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
         * =================================================
         * REFERENCE
         * =================================================
         */
        else if (
            type ==
            MOC_REFERENCE
        ) {

            /*
             * Need:
             *
             * 4 bytes offset
             * 4 bytes length
             */
            if (
                position + 8 >
                input_size
            ) {

                free(buffer);
                return 0;
            }


            uint32_t offset =
                read_u32(
                    input,
                    &position
                );


            uint32_t length =
                read_u32(
                    input,
                    &position
                );


            /*
             * Reference must point backwards.
             */
            if (
                offset == 0 ||
                offset > out
            ) {

                free(buffer);
                return 0;
            }


            size_t source =
                out - offset;


            /*
             * Copy byte-by-byte.
             *
             * This supports overlapping
             * LZ-style references.
             */
            for (
                uint32_t i = 0;
                i < length;
                i++
            ) {

                if (
                    out >=
                    original_size
                ) {

                    free(buffer);
                    return 0;
                }


                buffer[out++] =
                    buffer[
                        source + i
                    ];
            }
        }


        /*
         * Unknown token.
         */
        else {

            free(buffer);

            return 0;
        }
    }


    /*
     * The reconstructed file must have exactly
     * the size stored in the header.
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
