#include "../include/middleout.h"

#include <stdio.h>


void middleout_scan(
    const unsigned char *data,
    size_t size
) {
    if (size == 0) {
        return;
    }

    /*
     * Find the middle of the input.
     */
    size_t left = (size - 1) / 2;
    size_t right = size / 2;

    printf("Middle-Out traversal:\n");

    /*
     * Expand outward from the middle.
     */
    while (1) {

        printf("%02X ", data[left]);

        /*
         * If the input has an even number of bytes,
         * avoid printing the same middle byte twice.
         */
        if (right != left) {
            printf("%02X ", data[right]);
        }

        /*
         * Stop once we've reached both ends.
         */
        if (left == 0 && right == size - 1) {
            break;
        }

        /*
         * Expand toward the left.
         */
        if (left > 0) {
            left--;
        }

        /*
         * Expand toward the right.
         */
        if (right < size - 1) {
            right++;
        }
    }

    printf("\n");
}
