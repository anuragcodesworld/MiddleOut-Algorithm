#ifndef RLE_H
#define RLE_H

#include <stddef.h>

unsigned char *rle_compress(
    const unsigned char *input,
    size_t input_size,
    size_t *output_size
);

unsigned char *rle_decompress(
    const unsigned char *input,
    size_t input_size,
    size_t *output_size
);

#endif
