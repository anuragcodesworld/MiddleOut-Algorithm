#include "../include/reference.h"

#include <stddef.h>


#define MIN_MATCH 3
#define MAX_MATCH 32


Match find_best_match(
    const unsigned char *data,
    size_t size,
    size_t position
) {
    Match best = {
        position,
        0,
        0
    };

    if (position < MIN_MATCH) {
        return best;
    }


    size_t max_length = size - position;

    if (max_length > MAX_MATCH) {
        max_length = MAX_MATCH;
    }


    /*
     * Search backwards for a matching sequence.
     */
    for (
        size_t previous = 0;
        previous < position;
        previous++
    ) {

        size_t length = 0;


        while (
            length < max_length &&
            previous + length < position &&
            data[previous + length] ==
            data[position + length]
        ) {

            length++;
        }


        /*
         * Keep the longest match.
         */
        if (
            length >= MIN_MATCH &&
            length > best.length
        ) {

            best.offset =
                position - previous;

            best.length =
                length;
        }
    }


    return best;
}
