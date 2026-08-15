#include "../include/moc.h"
#include "../include/reference.h"

#include <stdint.h>
#include <stdlib.h>


#define MOC_LITERAL_BLOCK 0
#define MOC_REFERENCE     1

#define MIN_MATCH 3
#define MAX_MATCH 32

/*
 * Current reference encoding:
 *
 * 1 byte  token
 * 4 bytes offset
 * 4 bytes length
 *
 * Total = 9 bytes
 */
#define REFERENCE_COST 9


/*
 * =========================================================
 * WRITE / READ 32-BIT INTEGER
 * =========================================================
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
 * MATCH SCORE
 * =========================================================
 *
 * A reference is useful only if it saves bytes.
 *
 * Example:
 *
 * length = 20
 *
 * score = 20 - 9
 *       = 11
 *
 * So the reference potentially saves 11 bytes.
 */
static int calculate_match_score(
    size_t length,
    size_t offset
) {
    (void)offset;

    if (length < MIN_MATCH) {
        return -1000000;
    }

    return (int)length -
           REFERENCE_COST;
}


/*
 * =========================================================
 * MIDDLE-OUT MATCH SEARCH
 * =========================================================
 *
 * V3:
 *
 *     Find the longest match.
 *
 * V4:
 *
 *     Search candidates from the middle outward
 *     and score every candidate.
 *
 *     The candidate with the highest score wins.
 *
 * This keeps the "Middle-Out" idea while making
 * the decision measurable.
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


    if (position < MIN_MATCH) {
        return;
    }


    size_t maximum_length =
        size - position;


    if (maximum_length > MAX_MATCH) {
        maximum_length = MAX_MATCH;
    }


    /*
     * Search history.
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
         * Candidate on the left.
         */
        if (middle >= distance) {

            candidates[candidate_count++] =
                middle - distance;
        }


        /*
         * Candidate on the right.
         */
        if (
            distance != 0 &&
            middle + distance < history
        ) {

            candidates[candidate_count++] =
                middle + distance;
        }


        /*
         * Evaluate each candidate.
         */
        for (
            size_t c = 0;
            c < candidate_count;
            c++
        ) {

            size_t previous =
                candidates[c];


            /*
             * Candidate must be behind
             * the current position.
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


            /*
             * Calculate the economic value
             * of this candidate.
             */
            int score =
                calculate_match_score(
                    length,
                    offset
                );


            /*
             * Select the candidate with
             * the highest score.
             *
             * If scores are equal, prefer
             * the longer match.
             */
            if (
                score > *best_score ||
                (
                    score == *best_score &&
                    length > *best_length
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
 * =========================================================
 * MOC V4 COMPRESSOR
 * =========================================================
 */
int moc_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
) {
    /*
     * Generous allocation for the prototype.
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
     * MOC3
     * original size
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

        int score = -1000000;


        /*
         * Find the best Middle-Out candidate.
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
         * Only use a reference when it
         * actually saves bytes.
         */
        if (
            score > 0 &&
            length > 9
        ) {

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
         */
        size_t literal_start =
            position;


        size_t literal_length =
            0;


        while (
            position < input_size
        ) {

            size_t test_offset = 0;
            size_t test_length = 0;

            int test_score =
                -1000000;


            find_match_middle_out(
                input,
                input_size,
                position,
                &test_offset,
                &test_length,
                &test_score
            );


            /*
             * Stop collecting literals when
             * a profitable reference appears.
             */
            if (
                test_score > 0 &&
                test_length > 9
            ) {
                break;
            }


            position++;
            literal_length++;
        }


        /*
         * Literal block format:
         *
         * 1 byte token
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
 * MOC V4 DECOMPRESSOR
 * =========================================================
 *
 * File format has NOT changed.
 *
 * Therefore V3 and V4 compressed files use
 * the same decompression format.
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
     * Check MOC3 header.
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
         * =================================================
         * REFERENCE
         * =================================================
         */
        else if (
            type ==
            MOC_REFERENCE
        ) {

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


            if (
                offset == 0 ||
                offset > out
            ) {

                free(buffer);
                return 0;
            }


            size_t source =
                out - offset;


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
