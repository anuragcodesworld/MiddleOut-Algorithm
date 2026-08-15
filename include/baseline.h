#ifndef BASELINE_H
#define BASELINE_H

#include <stddef.h>

int baseline_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
);

#endif
