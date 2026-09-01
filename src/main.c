#define _POSIX_C_SOURCE 200809L

#include "../include/rle.h"
#include "../include/middleout.h"
#include "../include/pattern.h"
#include "../include/reference.h"
#include "../include/moc.h"
#include "../include/baseline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * ============================================================
 * FILE I/O
 * ============================================================
 */

/*
 * Read an entire file into memory.
 */
static unsigned char *read_file(
    const char *filename,
    size_t *size
) {
    FILE *file = fopen(filename, "rb");

    if (!file) {
        perror("Error opening input file");
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Error seeking input file");
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);

    if (file_size < 0) {
        perror("Error determining input file size");
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("Error rewinding input file");
        fclose(file);
        return NULL;
    }

    unsigned char *data = NULL;

    if (file_size > 0) {

        data = malloc((size_t)file_size);

        if (!data) {
            fprintf(
                stderr,
                "Error: memory allocation failed for input file.\n"
            );

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

            if (ferror(file)) {
                perror("Error reading input file");
            } else {
                fprintf(
                    stderr,
                    "Error: unexpected end of input file.\n"
                );
            }

            free(data);
            fclose(file);
            return NULL;
        }
    }

    if (fclose(file) != 0) {
        perror("Error closing input file");
        free(data);
        return NULL;
    }

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

    if (size > 0) {

        size_t written = fwrite(
            data,
            1,
            size,
            file
        );

        if (written != size) {

            if (ferror(file)) {
                perror("Error writing output file");
            } else {
                fprintf(
                    stderr,
                    "Error: incomplete output write.\n"
                );
            }

            fclose(file);
            return 0;
        }
    }

    if (fclose(file) != 0) {
        perror("Error closing output file");
        return 0;
    }

    return 1;
}


/*
 * ============================================================
 * FILE SAFETY
 * ============================================================
 */

/*
 * Check whether two existing paths refer to the same file.
 *
 * The output file may not exist yet. In that case realpath()
 * fails for the output and the files cannot currently be the
 * same existing file.
 */
/*
 * Check whether input and output refer to the same file.
 *
 * stat() compares the underlying filesystem object, so this
 * also protects against different path names referring to
 * the same file.
 */
static int same_file(
    const char *input,
    const char *output
) {
    struct stat input_stat;
    struct stat output_stat;

    /*
     * The input must exist because we already opened it.
     */
    if (stat(input, &input_stat) != 0) {
        return 0;
    }

    /*
     * The output may not exist yet.
     * If it doesn't exist, it cannot currently be the same
     * existing file.
     */
    if (stat(output, &output_stat) != 0) {
        return 0;
    }

    return (
        input_stat.st_dev == output_stat.st_dev &&
        input_stat.st_ino == output_stat.st_ino
    );
}


/*
 * ============================================================
 * HELP / VERSION
 * ============================================================
 */

static void print_usage(const char *program)
{
    printf("\n");
    printf("Middle-Out Compressor v10.0\n");
    printf("===========================\n\n");

    printf("Usage:\n");
    printf("  %s compress <input> <output>\n", program);
    printf("  %s decompress <input> <output>\n", program);

    printf("\nOptions:\n");
    printf("  -h, --help       Show this help message\n");
    printf("  -v, --version    Show version information\n");

    printf("\n");
}


/*
 * ============================================================
 * MIDDLE-OUT TRAVERSAL
 * ============================================================
 */

static int command_middle(
    const char *program,
    int argc,
    char *argv[]
) {
    if (argc < 3) {
        fprintf(
            stderr,
            "Usage: %s middle <input>\n",
            program
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
 * ============================================================
 * PATTERN DETECTION
 * ============================================================
 */

static int command_patterns(
    const char *program,
    int argc,
    char *argv[]
) {
    if (argc < 3) {
        fprintf(
            stderr,
            "Usage: %s patterns <input>\n",
            program
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
 * ============================================================
 * REFERENCE MATCH TEST
 * ============================================================
 */

static int command_match(
    const char *program,
    int argc,
    char *argv[]
) {
    if (argc < 3) {
        fprintf(
            stderr,
            "Usage: %s match <input>\n",
            program
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
 * ============================================================
 * BASELINE COMPRESSION
 * ============================================================
 */

static int command_baseline(
    const char *program,
    int argc,
    char *argv[]
) {
    if (argc < 4) {

        fprintf(
            stderr,
            "Usage: %s baseline <input> <output>\n",
            program
        );

        return 1;
    }

    const char *input_file = argv[2];
    const char *output_file = argv[3];

    if (same_file(input_file, output_file)) {
        fprintf(
            stderr,
            "Error: input and output files must be different.\n"
        );

        return 1;
    }

    size_t input_size = 0;

    unsigned char *input =
        read_file(
            input_file,
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

        fprintf(
            stderr,
            "Error: baseline compression failed.\n"
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
 * ============================================================
 * MOC COMPRESSION / DECOMPRESSION
 * ============================================================
 */

static int command_moc(
    const char *program,
    int argc,
    char *argv[]
) {
    if (argc < 4) {

        fprintf(
            stderr,
            "Usage:\n"
            "  %s compress <input> <output>\n"
            "  %s decompress <input> <output>\n",
            program,
            program
        );

        return 1;
    }

    const char *operation = argv[1];

    const char *input_file = argv[2];

    const char *output_file = argv[3];

    /*
     * Prevent accidental destruction of the input file.
     */
    if (same_file(input_file, output_file)) {

        fprintf(
            stderr,
            "Error: input and output files must be different.\n"
        );

        return 1;
    }

    /*
     * Read input.
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

    int success;

    if (strcmp(operation, "compress") == 0) {

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

    /*
     * Check operation result.
     */
    if (!success) {

        if (strcmp(operation, "decompress") == 0) {

            fprintf(
                stderr,
                "Error: invalid or corrupted MOC file.\n"
            );

        } else {

            fprintf(
                stderr,
                "Error: compression failed.\n"
            );
        }

        free(input);

        return 1;
    }

    /*
     * Write output.
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

    if (
        strcmp(operation, "compress") == 0 &&
        output_size > 0
    ) {

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
 * ============================================================
 * MAIN
 * ============================================================
 */

int main(int argc, char *argv[])
{
    /*
     * No command.
     */
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /*
     * Help.
     */
    if (
        strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0
    ) {

        print_usage(argv[0]);

        return 0;
    }

    /*
     * Version.
     */
    if (
        strcmp(argv[1], "--version") == 0 ||
        strcmp(argv[1], "-v") == 0
    ) {

        printf(
            "Middle-Out Compressor v10.0\n"
        );

        return 0;
    }

    /*
     * Development commands.
     */
    if (strcmp(argv[1], "middle") == 0) {
        return command_middle(
            argv[0],
            argc,
            argv
        );
    }

    if (strcmp(argv[1], "patterns") == 0) {
        return command_patterns(
            argv[0],
            argc,
            argv
        );
    }

    if (strcmp(argv[1], "match") == 0) {
        return command_match(
            argv[0],
            argc,
            argv
        );
    }

    if (strcmp(argv[1], "baseline") == 0) {
        return command_baseline(
            argv[0],
            argc,
            argv
        );
    }

    /*
     * Main MOC interface.
     *
     * We retain the old moc-compress and moc-decompress
     * commands for backwards compatibility.
     */
    if (
        strcmp(argv[1], "moc-compress") == 0
    ) {

        if (argc < 4) {

            fprintf(
                stderr,
                "Usage: %s moc-compress <input> <output>\n",
                argv[0]
            );

            return 1;
        }

        /*
         * Convert old command into the new internal interface.
         */
        char *new_argv[] = {
            argv[0],
            "compress",
            argv[2],
            argv[3]
        };

        return command_moc(
            argv[0],
            4,
            new_argv
        );
    }

    if (
        strcmp(argv[1], "moc-decompress") == 0
    ) {

        if (argc < 4) {

            fprintf(
                stderr,
                "Usage: %s moc-decompress <input> <output>\n",
                argv[0]
            );

            return 1;
        }

        char *new_argv[] = {
            argv[0],
            "decompress",
            argv[2],
            argv[3]
        };

        return command_moc(
            argv[0],
            4,
            new_argv
        );
    }

    /*
     * Main v10 interface.
     */
    if (
        strcmp(argv[1], "compress") == 0 ||
        strcmp(argv[1], "decompress") == 0
    ) {

        return command_moc(
            argv[0],
            argc,
            argv
        );
    }

    /*
     * Unknown command.
     */
    fprintf(
        stderr,
        "Error: unknown command '%s'\n",
        argv[1]
    );

    fprintf(
        stderr,
        "Use '%s --help' for usage information.\n",
        argv[0]
    );

    return 1;
}