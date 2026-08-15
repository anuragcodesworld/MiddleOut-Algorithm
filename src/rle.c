#include "../include/rle.h"

#include <stdlib.h>

unsigned char *rle_compress(
    const unsigned char *input,
    size_t input_size,
    size_t *output_size
) {
    if (input_size == 0) {
        *output_size = 0;
        return NULL;
    }

    /*
     * Worst case:
     * Every byte is different.
     *
     * Each input byte becomes:
     *     count + byte
     *
     * So we need at most 2 * input_size bytes.
     */
    unsigned char *output = malloc(input_size * 2);

    if (!output) {
        return NULL;
    }

    size_t in = 0;
    size_t out = 0;

    while (in < input_size) {

        unsigned char current = input[in];

        size_t count = 1;

        while (
            in + count < input_size &&
            input[in + count] == current &&
            count < 255
        ) {
            count++;
        }

        output[out++] = (unsigned char)count;
        output[out++] = current;

        in += count;
    }

    *output_size = out;

    return output;
}


unsigned char *rle_decompress(
    const unsigned char *input,
    size_t input_size,
    size_t *output_size
) {
    if (input_size == 0 || input_size % 2 != 0) {
        *output_size = 0;
        return NULL;
    }

    /*
     * Worst case:
     * Every encoded pair could contain count = 255.
     */
    size_t capacity = input_size * 255;

    unsigned char *output = malloc(capacity);

    if (!output) {
        return NULL;
    }

    size_t in = 0;
    size_t out = 0;

    while (in < input_size) {

        unsigned char count = input[in++];
        unsigned char value = input[in++];

        for (unsigned int i = 0; i < count; i++) {
            output[out++] = value;
        }
    }

    *output_size = out;

    return output;
}
