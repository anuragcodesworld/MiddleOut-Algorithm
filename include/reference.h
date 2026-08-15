#ifndef REFERENCE_H
#define REFERENCE_H

#include <stddef.h>

typedef struct {
    size_t position;
    size_t offset;
    size_t length;
} Match;

Match find_best_match(
    const unsigned char *data,
    size_t size,
    size_t position
);

#endif
