#include "../include/baseline.h"

#include <stdint.h>
#include <stdlib.h>

#define LITERAL 0
#define REFERENCE 1

#define MIN_MATCH 3
#define MAX_MATCH 32


static void write_u32(
    unsigned char *buffer,
    size_t *position,
    uint32_t value
) {
    buffer[(*position)++] = value & 0xFF;
    buffer[(*position)++] = (value >> 8) & 0xFF;
    buffer[(*position)++] = (value >> 16) & 0xFF;
    buffer[(*position)++] = (value >> 24) & 0xFF;
}


/*
 * Normal left-to-right LZ search.
 */
static void find_match(
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

    size_t maximum = size - position;

    if (maximum > MAX_MATCH) {
        maximum = MAX_MATCH;
    }

    /*
     * Search all previous positions.
     */
    for (
        size_t previous = 0;
        previous < position;
        previous++
    ) {

        size_t length = 0;

        while (
            length < maximum &&
            previous + length < position &&
            data[previous + length] ==
            data[position + length]
        ) {
            length++;
        }

        if (
            length >= MIN_MATCH &&
            length > *best_length
        ) {

            *best_length = length;

            *best_offset =
                position - previous;
        }
    }
}


int baseline_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
) {
    size_t capacity =
        input_size * 10 + 16;

    unsigned char *buffer =
        malloc(capacity);

    if (!buffer) {
        return 0;
    }

    size_t out = 0;

    /*
     * Baseline header.
     */
    buffer[out++] = 'B';
    buffer[out++] = 'L';
    buffer[out++] = 'Z';
    buffer[out++] = '1';

    write_u32(
        buffer,
        &out,
        (uint32_t)input_size
    );


    size_t position = 0;

    while (position < input_size) {

        size_t offset = 0;
        size_t length = 0;

        find_match(
            input,
            input_size,
            position,
            &offset,
            &length
        );


        if (length >= MIN_MATCH) {

            buffer[out++] = REFERENCE;

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

        } else {

            buffer[out++] = LITERAL;

            buffer[out++] =
                input[position];

            position++;
        }
    }


    *output = buffer;
    *output_size = out;

    return 1;
}
