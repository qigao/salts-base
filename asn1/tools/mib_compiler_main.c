#include "mib_internal.h"
#include <file/fs.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void print_usage(const char *program) {
    printf("Usage: %s [-o directory] [--no-c] [--no-runtime] file.mib\n"
           "Compiles the basic ASN.1 OBJECT IDENTIFIER subset.\n"
           "The output directory defaults to the current directory.\n", program);
}

int main(int argc, char **argv) {
    const char *input = NULL, *output = ".";
    int c = 1, runtime = 1;
    int directory_status;
    fs_stat_t status;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (++i == argc || !*argv[i]) {
                fprintf(stderr, "-o requires an output directory\n");
                return 1;
            }
            output = argv[i];
        } else if (strcmp(argv[i], "--no-c") == 0) c = 0;
        else if (strcmp(argv[i], "--no-runtime") == 0) runtime = 0;
        else if (*argv[i] == '-' || input) {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            return 1;
        } else input = argv[i];
    }
    if (!input) { print_usage(argv[0]); return 1; }
    directory_status = fs_stat(output, &status);
    if (directory_status == 0) {
        if (!status.is_directory) {
            fprintf(stderr, "Output path is not a directory: %s\n", output);
            return 1;
        }
    } else if (directory_status != -ENOENT) {
        fprintf(stderr, "Cannot inspect output directory %s: %d\n", output, directory_status);
        return 1;
    } else if (fs_mkdir(output, 0755) != 0) {
        fprintf(stderr, "Cannot create output directory: %s\n", output);
        return 1;
    }
    if (mib_compile_file_options(input, output, c, runtime) != 0) {
        fprintf(stderr, "Cannot compile %s into %s: invalid/unsupported input, resource limit or I/O failure\n",
                input, output);
        return 1;
    }
    return 0;
}
