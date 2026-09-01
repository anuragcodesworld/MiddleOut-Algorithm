#include "../include/moc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>

/*
 * ============================================================
 * MOC V10
 * ============================================================
 *
 * V10 hardening release.
 *
 * Preserves the V7.3/V9 compression design:
 *
 *   - MOC5 output format
 *   - MOC3/MOC5 decoder compatibility
 *   - variable-length integers
 *   - cost-aware references
 *   - 3-byte hash index
 *   - bounded-memory match index
 *   - maximum 64 candidates per position
 *   - overlapping matches supported
 *
 * V10 safety improvements:
 *
 *   - overflow-safe bounds checks
 *   - strict varint validation
 *   - strict header validation
 *   - strict literal bounds validation
 *   - strict reference validation
 *   - exact output-size validation
 *   - trailing-data rejection
 *   - safe output-buffer growth
 *   - NULL argument validation
 *
 * ============================================================
 */

#define MOC_LITERAL_BLOCK 0
#define MOC_REFERENCE     1

#define MIN_MATCH 3
#define MAX_MATCH 256

#define MAX_CANDIDATES 64

#define HASH_SIZE 65536

/*
 * Number of recent positions stored per hash bucket.
 *
 * Memory:
 *
 *     65536 * 8 * 4 = 2 MB
 *
 * This remains constant regardless of input size.
 */
#define CHAIN_LIMIT 8

#define INVALID_POSITION UINT32_MAX


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
            (unsigned char)(
                (value & 0x7F) | 0x80
            );

        value >>= 7;
    }

    buffer[(*position)++] =
        (unsigned char)value;
}


/*
 * Strict uint32 varint decoder.
 *
 * A uint32_t requires at most 5 bytes.
 *
 * The fifth byte may contain only one useful data bit:
 *
 *     0xxxxxxx
 *
 * where xxxxxxx must not represent a value above bit 31.
 */
static int read_varint(
    const unsigned char *buffer,
    size_t input_size,
    size_t *position,
    uint32_t *value
)
{
    if (
        buffer == NULL ||
        position == NULL ||
        value == NULL
    ) {
        return 0;
    }

    uint32_t result = 0;

    unsigned int shift = 0;

    for (unsigned int i = 0; i < 5; i++) {

        if (*position >= input_size) {
            return 0;
        }

        unsigned char byte =
            buffer[(*position)++];

        uint32_t payload =
            (uint32_t)(byte & 0x7F);

        /*
         * On the fifth byte only bit 31 may be used.
         */
        if (
            i == 4 &&
            payload > 1
        ) {
            return 0;
        }

        result |= payload << shift;

        /*
         * Final byte.
         */
        if ((byte & 0x80) == 0) {

            *value = result;

            return 1;
        }

        shift += 7;
    }

    /*
     * Five bytes were consumed and the fifth byte still
     * had the continuation bit set.
     */
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
 * BOUNDED HASH INDEX
 * ============================================================
 */

typedef struct HashIndex {

    uint32_t *positions;

    unsigned char *counts;

} HashIndex;


/*
 * ============================================================
 * INITIALIZE INDEX
 * ============================================================
 */

static int index_init(
    HashIndex *index
)
{
    if (index == NULL) {
        return 0;
    }

    index->positions = NULL;
    index->counts = NULL;

    size_t position_count =
        (size_t)HASH_SIZE * CHAIN_LIMIT;

    /*
     * Verify multiplication before allocation.
     */
    if (
        position_count >
        SIZE_MAX / sizeof(uint32_t)
    ) {
        return 0;
    }

    index->positions =
        malloc(
            position_count *
            sizeof(uint32_t)
        );

    if (!index->positions) {
        return 0;
    }

    index->counts =
        calloc(
            HASH_SIZE,
            sizeof(unsigned char)
        );

    if (!index->counts) {

        free(index->positions);

        index->positions = NULL;

        return 0;
    }

    for (
        size_t i = 0;
        i < position_count;
        i++
    ) {

        index->positions[i] =
            INVALID_POSITION;
    }

    return 1;
}


/*
 * ============================================================
 * FREE INDEX
 * ============================================================
 */

static void index_free(
    HashIndex *index
)
{
    if (index == NULL) {
        return;
    }

    free(index->positions);
    free(index->counts);

    index->positions = NULL;
    index->counts = NULL;
}


/*
 * ============================================================
 * ADD POSITION
 * ============================================================
 */

static int index_position(
    const unsigned char *data,
    size_t size,
    size_t position,
    HashIndex *index
)
{
    if (
        data == NULL ||
        index == NULL ||
        index->positions == NULL ||
        index->counts == NULL
    ) {
        return 0;
    }

    /*
     * Overflow-safe check:
     *
     * position + MIN_MATCH > size
     *
     * becomes:
     *
     * size - position < MIN_MATCH
     */
    if (
        position > size ||
        size - position < MIN_MATCH
    ) {
        return 1;
    }

    if (
        position >
        UINT32_MAX
    ) {
        return 0;
    }

    uint32_t hash =
        hash3(data + position);

    size_t base =
        (size_t)hash * CHAIN_LIMIT;

    unsigned int count =
        index->counts[hash];

    /*
     * Shift older entries toward the end.
     */
    if (count < CHAIN_LIMIT) {

        for (
            size_t i = count;
            i > 0;
            i--
        ) {

            index->positions[base + i] =
                index->positions[base + i - 1];
        }

        index->counts[hash] =
            (unsigned char)(count + 1);

    } else {

        for (
            size_t i = CHAIN_LIMIT - 1;
            i > 0;
            i--
        ) {

            index->positions[base + i] =
                index->positions[base + i - 1];
        }
    }

    index->positions[base] =
        (uint32_t)position;

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
    const HashIndex *index,
    size_t *best_offset,
    size_t *best_length,
    int *best_score
)
{
    *best_offset = 0;
    *best_length = 0;
    *best_score = -1000000;

    if (
        data == NULL ||
        index == NULL ||
        index->positions == NULL ||
        index->counts == NULL
    ) {
        return;
    }

    if (
        position > size ||
        size - position < MIN_MATCH
    ) {
        return;
    }

    uint32_t hash =
        hash3(data + position);

    size_t base =
        (size_t)hash * CHAIN_LIMIT;

    unsigned int count =
        index->counts[hash];

    size_t candidates = 0;

    /*
     * Candidates are newest to oldest.
     */
    for (
        unsigned int i = 0;
        i < count &&
        candidates < MAX_CANDIDATES;
        i++
    ) {

        uint32_t previous32 =
            index->positions[base + i];

        if (
            previous32 ==
            INVALID_POSITION
        ) {
            continue;
        }

        size_t previous =
            (size_t)previous32;

        if (
            previous >= position
        ) {
            continue;
        }

        size_t offset =
            position - previous;

        if (offset == 0) {
            continue;
        }

        size_t maximum_length =
            size - position;

        if (
            maximum_length >
            MAX_MATCH
        ) {
            maximum_length =
                MAX_MATCH;
        }

        size_t length = 0;

        /*
         * ----------------------------------------------------
         * Match calculation
         * ----------------------------------------------------
         *
         * Support overlapping LZ-style matches.
         *
         * The decoder repeats the source pattern when:
         *
         *     offset < length
         *
         * Therefore:
         *
         *     source = previous + (length % offset)
         *
         * ----------------------------------------------------
         */

        while (
            length < maximum_length
        ) {

            size_t source =
                previous +
                (length % offset);

            /*
             * Defensive check against addition overflow.
             */
            if (
                source < previous ||
                source >= size
            ) {
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

        if (
            length >= MIN_MATCH
        ) {

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
    }
}


/*
 * ============================================================
 * OUTPUT BUFFER GROWTH
 * ============================================================
 *
 * Compression output is normally less than input_size * 2.
 *
 * V10 does not rely on that assumption.
 *
 * The buffer grows dynamically if necessary.
 *
 * ============================================================
 */

static int ensure_capacity(
    unsigned char **buffer,
    size_t *capacity,
    size_t required
)
{
    if (
        buffer == NULL ||
        capacity == NULL
    ) {
        return 0;
    }

    if (required <= *capacity) {
        return 1;
    }

    size_t new_capacity =
        (*capacity == 0)
        ? 1024
        : *capacity;

    while (
        new_capacity < required
    ) {

        if (
            new_capacity >
            SIZE_MAX / 2
        ) {
            new_capacity = required;
            break;
        }

        new_capacity *= 2;
    }

    unsigned char *new_buffer =
        realloc(
            *buffer,
            new_capacity
        );

    if (!new_buffer) {
        return 0;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;

    return 1;
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
    if (
        output == NULL ||
        output_size == NULL
    ) {
        return 0;
    }

    *output = NULL;
    *output_size = 0;

    /*
     * MOC5 header stores original size in uint32_t.
     */
    if (
        input_size >
        UINT32_MAX
    ) {
        return 0;
    }

    /*
     * A non-empty input requires a valid input pointer.
     */
    if (
        input_size > 0 &&
        input == NULL
    ) {
        return 0;
    }

    /*
     * Start with the old V9 capacity.
     *
     * V10 can grow this buffer if required.
     */
    size_t capacity = 0;

    if (
        input_size <=
        (SIZE_MAX - 1024) / 2
    ) {
        capacity =
            input_size * 2 + 1024;

    } else {
        capacity = 1024;
    }

    /*
     * Never allow a zero allocation.
     */
    if (capacity < 8) {
        capacity = 8;
    }

    unsigned char *buffer =
        malloc(capacity);

    if (!buffer) {
        return 0;
    }

    /*
     * Initialize bounded hash index.
     */
    HashIndex index;

    if (
        !index_init(&index)
    ) {

        free(buffer);

        return 0;
    }

    size_t out = 0;

    /*
     * --------------------------------------------------------
     * MOC5 HEADER
     * --------------------------------------------------------
     */

    if (
        !ensure_capacity(
            &buffer,
            &capacity,
            8
        )
    ) {

        index_free(&index);
        free(buffer);

        return 0;
    }

    buffer[out++] = 'M';
    buffer[out++] = 'O';
    buffer[out++] = 'C';
    buffer[out++] = '5';

    uint32_t original_size =
        (uint32_t)input_size;

    buffer[out++] =
        (unsigned char)(
            original_size & 0xFF
        );

    buffer[out++] =
        (unsigned char)(
            (original_size >> 8) & 0xFF
        );

    buffer[out++] =
        (unsigned char)(
            (original_size >> 16) & 0xFF
        );

    buffer[out++] =
        (unsigned char)(
            (original_size >> 24) & 0xFF
        );

    size_t position = 0;

    /*
     * ========================================================
     * MAIN COMPRESSION LOOP
     * ========================================================
     */

    while (
        position < input_size
    ) {

        size_t offset = 0;
        size_t length = 0;
        int score = -1000000;

        find_match_indexed(
            input,
            input_size,
            position,
            &index,
            &offset,
            &length,
            &score
        );

        /*
         * ----------------------------------------------------
         * REFERENCE
         * ----------------------------------------------------
         */

        if (
            score > 0 &&
            length >= MIN_MATCH
        ) {

            size_t required =
                out +
                1 +
                varint_size((uint32_t)offset) +
                varint_size((uint32_t)length);

            /*
             * Check addition overflow.
             */
            if (
                required < out
            ) {

                index_free(&index);
                free(buffer);

                return 0;
            }

            if (
                !ensure_capacity(
                    &buffer,
                    &capacity,
                    required
                )
            ) {

                index_free(&index);
                free(buffer);

                return 0;
            }

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
                        &index
                    )
                ) {

                    index_free(&index);
                    free(buffer);

                    return 0;
                }
            }

            position += length;

            continue;
        }

        /*
         * ----------------------------------------------------
         * LITERAL BLOCK
         * ----------------------------------------------------
         */

        size_t literal_start =
            position;

        size_t literal_length = 0;

        while (
            position < input_size
        ) {

            size_t test_offset = 0;
            size_t test_length = 0;
            int test_score = -1000000;

            find_match_indexed(
                input,
                input_size,
                position,
                &index,
                &test_offset,
                &test_length,
                &test_score
            );

            /*
             * Stop the literal block as soon as a profitable
             * reference is available.
             */
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
                    &index
                )
            ) {

                index_free(&index);
                free(buffer);

                return 0;
            }

            position++;
            literal_length++;
        }

        /*
         * Literal token requires:
         *
         *     1 byte type
         *     varint length
         *     literal bytes
         */

        if (
            literal_length >
            UINT32_MAX
        ) {

            index_free(&index);
            free(buffer);

            return 0;
        }

        size_t literal_header =
            1 +
            varint_size(
                (uint32_t)literal_length
            );

        if (
            literal_header >
            SIZE_MAX - literal_length
        ) {

            index_free(&index);
            free(buffer);

            return 0;
        }

        size_t required =
            out +
            literal_header +
            literal_length;

        if (
            required < out
        ) {

            index_free(&index);
            free(buffer);

            return 0;
        }

        if (
            !ensure_capacity(
                &buffer,
                &capacity,
                required
            )
        ) {

            index_free(&index);
            free(buffer);

            return 0;
        }

        buffer[out++] =
            MOC_LITERAL_BLOCK;

        write_varint(
            buffer,
            &out,
            (uint32_t)literal_length
        );

        if (literal_length > 0) {

            memcpy(
                buffer + out,
                input + literal_start,
                literal_length
            );

            out += literal_length;
        }
    }

    index_free(&index);

    *output = buffer;
    *output_size = out;

    return 1;
}


/*
 * ============================================================
 * DECOMPRESSOR
 * ============================================================
 *
 * Supports:
 *
 *     MOC3
 *     MOC5
 *
 * V10 performs strict validation of every field.
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
    if (
        output == NULL ||
        output_size == NULL
    ) {
        return 0;
    }

    *output = NULL;
    *output_size = 0;

    /*
     * Header:
     *
     *     4 bytes magic
     *     4 bytes original size
     *
     * Therefore at least 8 bytes are required.
     */
    if (
        input == NULL ||
        input_size < 8
    ) {
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

    if (
        !is_moc3 &&
        !is_moc5
    ) {
        return 0;
    }

    size_t position = 4;

    /*
     * --------------------------------------------------------
     * Original size
     * --------------------------------------------------------
     */

    /*
     * We already know input_size >= 8, therefore these four
     * reads are safe.
     */
    uint32_t original_size =
        (uint32_t)input[position++];

    original_size |=
        (uint32_t)input[position++] << 8;

    original_size |=
        (uint32_t)input[position++] << 16;

    original_size |=
        (uint32_t)input[position++] << 24;

    /*
     * Empty file:
     *
     * A valid empty MOC file contains exactly the 8-byte header.
     */
    if (
        original_size == 0
    ) {

        if (
            position != input_size
        ) {
            return 0;
        }

        /*
         * Allocate one byte so that success can still return a
         * valid owned pointer.
         */
        unsigned char *empty =
            malloc(1);

        if (!empty) {
            return 0;
        }

        *output = empty;
        *output_size = 0;

        return 1;
    }

    /*
     * Allocate output buffer.
     *
     * original_size is uint32_t, therefore this conversion to
     * size_t is safe on normal supported 32/64-bit platforms.
     */
    size_t expected_size =
        (size_t)original_size;

    unsigned char *buffer =
        malloc(expected_size);

    if (!buffer) {
        return 0;
    }

    size_t out = 0;

    /*
     * ========================================================
     * DECODE BLOCKS
     * ========================================================
     */

    while (
        position < input_size
    ) {

        /*
         * Once the declared output size has been reached,
         * there must not be another token.
         */
        if (
            out == expected_size
        ) {
            free(buffer);
            return 0;
        }

        /*
         * Safe because position < input_size.
         */
        unsigned char type =
            input[position++];

        /*
         * ----------------------------------------------------
         * LITERAL
         * ----------------------------------------------------
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

                /*
                 * MOC3 uses a fixed 32-bit length.
                 */
                if (
                    input_size - position < 4
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

            size_t literal_length =
                (size_t)length;

            /*
             * Prevent:
             *
             *     position + length
             *
             * from overflowing.
             */
            if (
                literal_length >
                input_size - position
            ) {

                free(buffer);
                return 0;
            }

            /*
             * Prevent output overflow.
             */
            if (
                literal_length >
                expected_size - out
            ) {

                free(buffer);
                return 0;
            }

            if (literal_length > 0) {

                memcpy(
                    buffer + out,
                    input + position,
                    literal_length
                );

                position += literal_length;
                out += literal_length;
            }
        }

        /*
         * ----------------------------------------------------
         * REFERENCE
         * ----------------------------------------------------
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

                /*
                 * MOC3 reference:
                 *
                 *     uint32 offset
                 *     uint32 length
                 */
                if (
                    input_size - position < 8
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

            /*
             * A reference must have:
             *
             *     offset > 0
             *     offset <= bytes already produced
             *     length <= remaining output capacity
             */
            if (
                offset == 0
            ) {

                free(buffer);
                return 0;
            }

            size_t reference_offset =
                (size_t)offset;

            size_t reference_length =
                (size_t)length;

            if (
                reference_offset > out
            ) {

                free(buffer);
                return 0;
            }

            if (
                reference_length >
                expected_size - out
            ) {

                free(buffer);
                return 0;
            }

            /*
             * Reference source begins 'offset' bytes behind
             * the current output position.
             */
            size_t source =
                out - reference_offset;

            /*
             * source is guaranteed to be < out because:
             *
             *     offset > 0
             *     offset <= out
             */
            for (
                size_t i = 0;
                i < reference_length;
                i++
            ) {

                /*
                 * Overlapping references are intentionally
                 * supported.
                 *
                 * As out grows, buffer[source + i] may refer
                 * to bytes written earlier in this same copy.
                 */
                buffer[out++] =
                    buffer[source + i];
            }
        }

        /*
         * ----------------------------------------------------
         * INVALID BLOCK TYPE
         * ----------------------------------------------------
         */

        else {

            free(buffer);
            return 0;
        }
    }

    /*
     * ========================================================
     * FINAL INTEGRITY CHECK
     * ========================================================
     *
     * A valid file must:
     *
     *   1. produce exactly original_size bytes
     *   2. consume the entire input
     *
     * Because the decoding loop already stops only when
     * position == input_size, condition #2 is guaranteed here.
     * We retain the explicit check for clarity and defense.
     * ========================================================
     */

    if (
        out != expected_size ||
        position != input_size
    ) {

        free(buffer);
        return 0;
    }

    *output = buffer;
    *output_size = out;

    return 1;
}