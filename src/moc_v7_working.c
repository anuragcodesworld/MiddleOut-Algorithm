#include "../include/moc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * ============================================================
 * MOC V7
 * ============================================================
 *
 * V7 keeps the MOC5 file format and decoder, but replaces the
 * heap-allocated linked-list match index used by V6 with an
 * array-based hash chain.
 *
 * V7 features:
 *
 *   - MOC5 compatible output
 *   - MOC3/MOC5 compatible decoder
 *   - variable-length integers
 *   - cost-aware references
 *   - 3-byte hash index
 *   - array-based hash chains
 *   - no malloc/free per indexed position
 *
 */

#define MOC_LITERAL_BLOCK 0
#define MOC_REFERENCE     1

#define MIN_MATCH 3
#define MAX_MATCH 256

#define MAX_CANDIDATES 64

#define HASH_SIZE 65536

/*
 * ============================================================
 * VARINT
 * ============================================================
 */

static size_t varint_size(uint32_t value)
{
    size_t size = 1;

    while (value >= 128) {
        value >>= 7;
        size++;
    }

    return size;
}

static void write_varint(
    unsigned char *buffer,
    size_t *position,
    uint32_t value
)
{
    while (value >= 128) {

        buffer[(*position)++] =
            (unsigned char)((value & 0x7F) | 0x80);

        value >>= 7;
    }

    buffer[(*position)++] =
        (unsigned char)value;
}

static int read_varint(
    const unsigned char *buffer,
    size_t input_size,
    size_t *position,
    uint32_t *value
)
{
    uint32_t result = 0;
    unsigned int shift = 0;

    while (*position < input_size) {

        unsigned char byte =
            buffer[(*position)++];

        result |=
            (uint32_t)(byte & 0x7F) << shift;

        if ((byte & 0x80) == 0) {

            *value = result;

            return 1;
        }

        shift += 7;

        if (shift >= 32) {
            return 0;
        }
    }

    return 0;
}

/*
 * ============================================================
 * HASH
 * ============================================================
 */

static uint32_t hash3(
    const unsigned char *data
)
{
    uint32_t value =
        ((uint32_t)data[0] << 16) |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2]);

    value ^= value >> 11;

    value *= 2654435761u;

    value ^= value >> 16;

    return value & (HASH_SIZE - 1);
}

/*
 * ============================================================
 * REFERENCE COST
 * ============================================================
 */

static size_t reference_cost(
    size_t offset,
    size_t length
)
{
    return
        1 +
        varint_size((uint32_t)offset) +
        varint_size((uint32_t)length);
}

/*
 * ============================================================
 * ARRAY HASH CHAIN
 * ============================================================
 *
 * head[hash] stores the newest position belonging to a hash.
 *
 * prev[position] stores the previous position belonging to
 * the same hash bucket.
 *
 * This completely removes the per-position malloc/free cost
 * from V6.
 *
 * ============================================================
 */

static int index_position(
    const unsigned char *data,
    size_t size,
    size_t position,
    int32_t *head,
    int32_t *prev
)
{
    if (position + MIN_MATCH > size) {
        return 1;
    }

    uint32_t hash =
        hash3(data + position);

    /*
     * Positions are stored as int32_t because the MOC5 format
     * itself uses uint32_t offsets.
     */
    if (position > INT32_MAX) {
        return 0;
    }

    int32_t old_head =
        head[hash];

    prev[position] =
        old_head;

    head[hash] =
        (int32_t)position;

    return 1;
}

/*
 * ============================================================
 * FIND BEST MATCH
 * ============================================================
 */

static void find_match_indexed(
    const unsigned char *data,
    size_t size,
    size_t position,
    const int32_t *head,
    const int32_t *prev,
    size_t *best_offset,
    size_t *best_length,
    int *best_score
)
{
    *best_offset = 0;

    *best_length = 0;

    *best_score = -1000000;

    if (position + MIN_MATCH > size) {
        return;
    }

    uint32_t hash =
        hash3(data + position);

    int32_t node =
        head[hash];

    size_t candidates = 0;

    while (
        node >= 0 &&
        candidates < MAX_CANDIDATES
    ) {

        size_t previous =
            (size_t)node;

        /*
         * Safety.
         */
        if (previous >= position) {
            node = prev[previous];
            continue;
        }

        size_t offset =
            position - previous;

        if (offset == 0) {
            node = prev[previous];
            continue;
        }

        size_t maximum_length =
            size - position;

        if (maximum_length > MAX_MATCH) {
            maximum_length = MAX_MATCH;
        }

        size_t length = 0;

        /*
         * Overlapping matches are supported.
         */
        while (length < maximum_length) {

            size_t source =
                previous + (length % offset);

            if (source >= position) {
                break;
            }

            if (
                data[source] !=
                data[position + length]
            ) {
                break;
            }

            length++;
        }

        if (length >= MIN_MATCH) {

            size_t cost =
                reference_cost(
                    offset,
                    length
                );

            int score =
                (int)length -
                (int)cost;

            if (
                score > *best_score ||
                (
                    score == *best_score &&
                    length > *best_length
                ) ||
                (
                    score == *best_score &&
                    length == *best_length &&
                    (
                        *best_offset == 0 ||
                        offset < *best_offset
                    )
                )
            ) {

                *best_score =
                    score;

                *best_length =
                    length;

                *best_offset =
                    offset;
            }
        }

        candidates++;

        node = prev[previous];
    }
}

/*
 * ============================================================
 * COMPRESSOR
 * ============================================================
 */

int moc_compress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
)
{
    /*
     * The MOC5 header stores the original size in uint32_t.
     */
    if (input_size > UINT32_MAX) {
        return 0;
    }

    /*
     * Output buffer.
     */
    size_t capacity =
        input_size * 2 + 1024;

    unsigned char *buffer =
        malloc(capacity);

    if (!buffer) {
        return 0;
    }

    /*
     * Array-based hash index.
     */
    int32_t *head =
        malloc(
            HASH_SIZE * sizeof(int32_t)
        );

    if (!head) {
        free(buffer);
        return 0;
    }

    /*
     * One previous-position entry per input byte.
     *
     * For very large files this is approximately 4 bytes/input
     * byte. This is still much cheaper than a heap allocation
     * per indexed position.
     */
    int32_t *prev =
        malloc(
            input_size * sizeof(int32_t)
        );

    if (!prev && input_size > 0) {

        free(head);
        free(buffer);

        return 0;
    }

    /*
     * Initialize hash heads to -1.
     */
    for (size_t i = 0; i < HASH_SIZE; i++) {
        head[i] = -1;
    }

    size_t out = 0;

    /*
     * MOC5 header.
     */
    buffer[out++] = 'M';
    buffer[out++] = 'O';
    buffer[out++] = 'C';
    buffer[out++] = '5';

    uint32_t original_size =
        (uint32_t)input_size;

    buffer[out++] =
        (unsigned char)(original_size & 0xFF);

    buffer[out++] =
        (unsigned char)((original_size >> 8) & 0xFF);

    buffer[out++] =
        (unsigned char)((original_size >> 16) & 0xFF);

    buffer[out++] =
        (unsigned char)((original_size >> 24) & 0xFF);

    size_t position = 0;

    while (position < input_size) {

        size_t offset = 0;
        size_t length = 0;

        int score = -1000000;

        find_match_indexed(
            input,
            input_size,
            position,
            head,
            prev,
            &offset,
            &length,
            &score
        );

        /*
         * Reference block.
         */
        if (
            score > 0 &&
            length >= MIN_MATCH
        ) {

            buffer[out++] =
                MOC_REFERENCE;

            write_varint(
                buffer,
                &out,
                (uint32_t)offset
            );

            write_varint(
                buffer,
                &out,
                (uint32_t)length
            );

            /*
             * Index all consumed positions.
             */
            for (
                size_t i = 0;
                i < length;
                i++
            ) {

                if (
                    !index_position(
                        input,
                        input_size,
                        position + i,
                        head,
                        prev
                    )
                ) {

                    free(prev);
                    free(head);
                    free(buffer);

                    return 0;
                }
            }

            position += length;

            continue;
        }

        /*
         * Literal block.
         */
        size_t literal_start =
            position;

        size_t literal_length = 0;

        while (position < input_size) {

            size_t test_offset = 0;
            size_t test_length = 0;

            int test_score = -1000000;

            find_match_indexed(
                input,
                input_size,
                position,
                head,
                prev,
                &test_offset,
                &test_length,
                &test_score
            );

            if (
                test_score > 0 &&
                test_length >= MIN_MATCH
            ) {
                break;
            }

            if (
                !index_position(
                    input,
                    input_size,
                    position,
                    head,
                    prev
                )
            ) {

                free(prev);
                free(head);
                free(buffer);

                return 0;
            }

            position++;
            literal_length++;
        }

        buffer[out++] =
            MOC_LITERAL_BLOCK;

        write_varint(
            buffer,
            &out,
            (uint32_t)literal_length
        );

        memcpy(
            buffer + out,
            input + literal_start,
            literal_length
        );

        out += literal_length;
    }

    free(prev);
    free(head);

    *output =
        buffer;

    *output_size =
        out;

    return 1;
}

/*
 * ============================================================
 * DECOMPRESSOR
 * ============================================================
 *
 * Supports MOC3 and MOC5.
 *
 * This is intentionally kept compatible with V6.
 *
 * ============================================================
 */

int moc_decompress(
    const unsigned char *input,
    size_t input_size,
    unsigned char **output,
    size_t *output_size
)
{
    if (input_size < 8) {
        return 0;
    }

    int is_moc3 =
        input[0] == 'M' &&
        input[1] == 'O' &&
        input[2] == 'C' &&
        input[3] == '3';

    int is_moc5 =
        input[0] == 'M' &&
        input[1] == 'O' &&
        input[2] == 'C' &&
        input[3] == '5';

    if (!is_moc3 && !is_moc5) {
        return 0;
    }

    size_t position = 4;

    uint32_t original_size =
        (uint32_t)input[position++];

    original_size |=
        (uint32_t)input[position++] << 8;

    original_size |=
        (uint32_t)input[position++] << 16;

    original_size |=
        (uint32_t)input[position++] << 24;

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
         * Literal.
         */
        if (
            type ==
            MOC_LITERAL_BLOCK
        ) {

            uint32_t length = 0;

            if (is_moc5) {

                if (
                    !read_varint(
                        input,
                        input_size,
                        &position,
                        &length
                    )
                ) {

                    free(buffer);

                    return 0;
                }

            } else {

                if (
                    position + 4 >
                    input_size
                ) {

                    free(buffer);

                    return 0;
                }

                length =
                    (uint32_t)input[position++];

                length |=
                    (uint32_t)input[position++] << 8;

                length |=
                    (uint32_t)input[position++] << 16;

                length |=
                    (uint32_t)input[position++] << 24;
            }

            if (
                position + length >
                input_size ||
                out + length >
                original_size
            ) {

                free(buffer);

                return 0;
            }

            memcpy(
                buffer + out,
                input + position,
                length
            );

            position += length;

            out += length;
        }

        /*
         * Reference.
         */
        else if (
            type ==
            MOC_REFERENCE
        ) {

            uint32_t offset = 0;
            uint32_t length = 0;

            if (is_moc5) {

                if (
                    !read_varint(
                        input,
                        input_size,
                        &position,
                        &offset
                    )
                ) {

                    free(buffer);

                    return 0;
                }

                if (
                    !read_varint(
                        input,
                        input_size,
                        &position,
                        &length
                    )
                ) {

                    free(buffer);

                    return 0;
                }

            } else {

                if (
                    position + 8 >
                    input_size
                ) {

                    free(buffer);

                    return 0;
                }

                offset =
                    (uint32_t)input[position++];

                offset |=
                    (uint32_t)input[position++] << 8;

                offset |=
                    (uint32_t)input[position++] << 16;

                offset |=
                    (uint32_t)input[position++] << 24;

                length =
                    (uint32_t)input[position++];

                length |=
                    (uint32_t)input[position++] << 8;

                length |=
                    (uint32_t)input[position++] << 16;

                length |=
                    (uint32_t)input[position++] << 24;
            }

            if (
                offset == 0 ||
                offset > out ||
                out + length >
                original_size
            ) {

                free(buffer);

                return 0;
            }

            size_t source =
                out - offset;

            /*
             * Overlapping copy.
             */
            for (
                uint32_t i = 0;
                i < length;
                i++
            ) {

                buffer[out++] =
                    buffer[source + i];
            }
        }

        else {

            free(buffer);

            return 0;
        }
    }

    if (out != original_size) {

        free(buffer);

        return 0;
    }

    *output =
        buffer;

    *output_size =
        out;

    return 1;
}
