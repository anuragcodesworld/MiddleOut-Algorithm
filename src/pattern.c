#include "../include/pattern.h"

#include <stdio.h>
#include <stddef.h>


#define MIN_PATTERN 3
#define MAX_PATTERN 8


static int same_bytes(
    const unsigned char *data,
    size_t a,
    size_t b,
    size_t length
) {
    for (size_t i = 0; i < length; i++) {

        if (data[a + i] != data[b + i]) {
            return 0;
        }
    }

    return 1;
}


void find_patterns(
    const unsigned char *data,
    size_t size
) {
    if (size < MIN_PATTERN * 2) {
        printf("Not enough data for pattern detection.\n");
        return;
    }

    size_t middle = size / 2;

    printf("Middle-Out Pattern Detection\n");
    printf("============================\n");

    /*
     * Start around the middle and move outward.
     */
    for (
        size_t distance = 0;
        distance <= middle;
        distance++
    ) {

        size_t positions[2];
        size_t count = 0;

        if (middle >= distance) {
            positions[count++] = middle - distance;
        }

        if (
            distance != 0 &&
            middle + distance < size
        ) {
            positions[count++] = middle + distance;
        }

        for (size_t p = 0; p < count; p++) {

            size_t start = positions[p];

            /*
             * Don't read past the end.
             */
            for (
                size_t length = MIN_PATTERN;
                length <= MAX_PATTERN;
                length++
            ) {

                if (start + length > size) {
                    break;
                }

                /*
                 * Search for this pattern elsewhere.
                 */
                for (size_t other = 0; other + length <= size; other++) {

                    if (other == start) {
                        continue;
                    }

                    if (same_bytes(
                        data,
                        start,
                        other,
                        length
                    )) {

                        printf(
                            "Pattern found: position=%zu "
                            "length=%zu\n",
                            start,
                            length
                        );

                        break;
                    }
                }
            }
        }
    }
}
