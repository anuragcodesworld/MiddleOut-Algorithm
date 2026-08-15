#include "../include/rle.h"
#include "../include/middleout.h"
#include "../include/pattern.h"
#include "../include/reference.h"
#include "../include/moc.h"
#include "../include/baseline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * Read an entire file into memory.
 */
static unsigned char *read_file(
    const char *filename,
    size_t *size
) {
    FILE *file = fopen(filename, "rb");

    if (!file) {
        perror("Error opening file");
        return NULL;
    }

    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    rewind(file);

    if (file_size < 0) {
        printf("Could not determine file size.\n");
        fclose(file);
        return NULL;
    }

    unsigned char *data = NULL;

    if (file_size > 0) {

        data = malloc((size_t)file_size);

        if (!data) {
            printf("Memory allocation failed.\n");
            fclose(file);
            return NULL;
        }

        size_t bytes_read = fread(
            data,
            1,
            (size_t)file_size,
            file
        );

        if (bytes_read != (size_t)file_size) {
            printf("Error reading file.\n");

            free(data);
            fclose(file);

            return NULL;
        }
    }

    fclose(file);

    *size = (size_t)file_size;

    return data;
}


/*
 * Write data to a file.
 */
static int write_file(
    const char *filename,
    const unsigned char *data,
    size_t size
) {
    FILE *file = fopen(filename, "wb");

    if (!file) {
        perror("Error opening output file");
        return 0;
    }

    size_t written = fwrite(
        data,
        1,
        size,
        file
    );

    fclose(file);

    if (written != size) {
        printf("Error writing output file.\n");
        return 0;
    }

    return 1;
}


/*
 * Print program usage.
 */
static void print_usage(const char *program) {

    printf("\n");
    printf("Middle-Out Compressor\n");
    printf("=====================\n\n");

    printf("Usage:\n");

    printf("  %s compress <input> <output>\n", program);

    printf("  %s decompress <input> <output>\n", program);

    printf("  %s middle <input>\n", program);

    printf("  %s patterns <input>\n", program);

    printf("  %s match <input>\n", program);

   printf(
        "  %s moc-compress <input> <output>\n",
        program
    );

    printf(
        "  %s moc-decompress <input> <output>\n",
        program
    );
    printf(
    "  %s baseline <input> <output>\n",
    program
);

    printf("\n");
}


int main(int argc, char *argv[]) {

    /*
     * No command provided.
     */
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }


    /*
     * =========================================================
     * MIDDLE-OUT TRAVERSAL
     * =========================================================
     */

    if (strcmp(argv[1], "middle") == 0) {

        if (argc < 3) {
            printf(
                "Usage: %s middle <input>\n",
                argv[0]
            );

            return 1;
        }

        const char *filename = argv[2];

        size_t size = 0;

        unsigned char *data = read_file(
            filename,
            &size
        );

        if (!data && size != 0) {
            return 1;
        }

        printf("\n");

        middleout_scan(
            data,
            size
        );

        printf("\n");

        free(data);

        return 0;
    }


    /*
     * =========================================================
     * MIDDLE-OUT PATTERN DETECTION
     * =========================================================
     */

    if (strcmp(argv[1], "patterns") == 0) {

        if (argc < 3) {
            printf(
                "Usage: %s patterns <input>\n",
                argv[0]
            );

            return 1;
        }

        const char *filename = argv[2];

        size_t size = 0;

        unsigned char *data = read_file(
            filename,
            &size
        );

        if (!data && size != 0) {
            return 1;
        }

        printf("\n");

        find_patterns(
            data,
            size
        );

        printf("\n");

        free(data);

        return 0;
    }


    /*
     * =========================================================
     * REFERENCE MATCH TEST
     * =========================================================
     */

    if (strcmp(argv[1], "match") == 0) {

        if (argc < 3) {
            printf(
                "Usage: %s match <input>\n",
                argv[0]
            );

            return 1;
        }

        const char *filename = argv[2];

        size_t size = 0;

        unsigned char *data = read_file(
            filename,
            &size
        );

        if (!data && size != 0) {
            return 1;
        }

        printf("\n");
        printf("Reference Matches\n");
        printf("=================\n");

        /*
         * Check every position for a previous match.
         */
        for (
            size_t position = 0;
            position < size;
            position++
        ) {

            Match match = find_best_match(
                data,
                size,
                position
            );

            if (match.length >= 3) {

                printf(
                    "position=%zu  "
                    "offset=%zu  "
                    "length=%zu\n",

                    match.position,
                    match.offset,
                    match.length
                );
            }
        }

        printf("\n");

        free(data);

        return 0;
    }


    /*
     * =========================================================
     * BASELINE LZ COMPRESSION
     * =========================================================
     */

    if (strcmp(argv[1], "baseline") == 0) {

        if (argc < 4) {

            printf(
                "Usage: %s baseline <input> <output>\n",
                argv[0]
            );

            return 1;
        }


        size_t input_size = 0;

        unsigned char *input =
            read_file(
                argv[2],
                &input_size
            );


        if (!input && input_size != 0) {
            return 1;
        }


        unsigned char *output = NULL;

        size_t output_size = 0;


        if (!baseline_compress(
            input,
            input_size,
            &output,
            &output_size
        )) {

            printf(
                "Baseline compression failed.\n"
            );

            free(input);

            return 1;
        }


        if (!write_file(
            argv[3],
            output,
            output_size
        )) {

            free(input);
            free(output);

            return 1;
        }


        printf("\n");

        printf(
            "Input size : %zu bytes\n",
            input_size
        );

        printf(
            "Output size: %zu bytes\n",
            output_size
        );

        if (output_size > 0) {

            printf(
                "Ratio      : %.2fx\n",
                (double)input_size /
                (double)output_size
            );
        }

        printf("Done!\n\n");


        free(input);
        free(output);

        return 0;
    }

    /*
     * =========================================================
     * MIDDLE-OUT COMPRESS / DECOMPRESS
     * =========================================================
     */

    if (
        strcmp(argv[1], "moc-compress") == 0 ||
        strcmp(argv[1], "moc-decompress") == 0
    ) {

        if (argc < 4) {
            printf(
                "Usage:\n"
                "  %s moc-compress <input> <output>\n"
                "  %s moc-decompress <input> <output>\n",
                argv[0],
                argv[0]
            );

            return 1;
        }


        const char *operation = argv[1];

        const char *input_file = argv[2];

        const char *output_file = argv[3];


        size_t input_size = 0;

        unsigned char *input = read_file(
            input_file,
            &input_size
        );

        if (!input && input_size != 0) {
            return 1;
        }


        unsigned char *output = NULL;

        size_t output_size = 0;


        int success;


        if (
            strcmp(operation, "moc-compress") == 0
        ) {

            success = moc_compress(
                input,
                input_size,
                &output,
                &output_size
            );

        } else {

            success = moc_decompress(
                input,
                input_size,
                &output,
                &output_size
            );
        }


        if (!success) {

            printf(
                "Middle-Out operation failed.\n"
            );

            free(input);

            return 1;
        }


        if (!write_file(
            output_file,
            output,
            output_size
        )) {

            free(input);
            free(output);

            return 1;
        }


        printf("\n");

        printf(
            "Input size : %zu bytes\n",
            input_size
        );

        printf(
            "Output size: %zu bytes\n",
            output_size
        );


        if (output_size > 0) {

            printf(
                "Ratio      : %.2fx\n",
                (double)input_size /
                (double)output_size
            );
        }


        printf("Done!\n\n");


        free(input);
        free(output);

        return 0;
    }

    /*
     * =========================================================
     * COMPRESS / DECOMPRESS
     * =========================================================
     */

    if (
        strcmp(argv[1], "compress") == 0 ||
        strcmp(argv[1], "decompress") == 0
    ) {

        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }

        const char *operation = argv[1];

        const char *input_file = argv[2];

        const char *output_file = argv[3];


        /*
         * Read input file.
         */

        size_t input_size = 0;

        unsigned char *input = read_file(
            input_file,
            &input_size
        );

        if (!input && input_size != 0) {
            return 1;
        }


        /*
         * Perform operation.
         */

        unsigned char *output = NULL;

        size_t output_size = 0;


        if (strcmp(operation, "compress") == 0) {

            output = rle_compress(
                input,
                input_size,
                &output_size
            );

        } else {

            output = rle_decompress(
                input,
                input_size,
                &output_size
            );
        }


        /*
         * Check result.
         */

        if (!output && output_size != 0) {

            printf(
                "Compression/decompression failed.\n"
            );

            free(input);

            return 1;
        }


        /*
         * Write output file.
         */

        if (!write_file(
            output_file,
            output,
            output_size
        )) {

            free(input);
            free(output);

            return 1;
        }


        /*
         * Display statistics.
         */

        printf("\n");

        printf(
            "Input size : %zu bytes\n",
            input_size
        );

        printf(
            "Output size: %zu bytes\n",
            output_size
        );


        if (output_size > 0) {

            printf(
                "Ratio      : %.2fx\n",
                (double)input_size /
                (double)output_size
            );
        }

        printf("Done!\n\n");


        free(input);
        free(output);

        return 0;
    }


    /*
     * Unknown command.
     */

    printf(
        "Unknown command: %s\n",
        argv[1]
    );

    print_usage(argv[0]);

    return 1;
}
