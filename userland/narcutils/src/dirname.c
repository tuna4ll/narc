#include <narcutils/filenames.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "dirname"

static const struct option long_options[] = {
    { "zero", no_argument, 0, 'z' },
    GETOPT_HELP_OPTION_DECL,
    GETOPT_VERSION_OPTION_DECL,
    { 0, 0, 0, 0 },
};

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION] NAME...\n", program_name);
    fputs("Output each NAME with its last non-slash component and trailing slashes\n"
          "removed; if NAME contains no /'s, output '.' (meaning the current directory).\n\n"
          "  -z, --zero     end each output line with NUL, not newline\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    printf("\nExamples:\n"
           "  %s /usr/bin/          -> \"/usr\"\n"
           "  %s dir1/str dir2/str  -> \"dir1\" followed by \"dir2\"\n"
           "  %s stdio.h            -> \".\"\n",
           program_name, program_name, program_name);
    exit(status);
}

int main(int argc, char **argv) {
    bool zero = false;
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "z", long_options, 0)) != -1) {
        switch (c) {
        case 'z': zero = true; break;
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
        default: usage(EXIT_FAILURE);
        }
    }
    if (argc < optind + 1) {
        error(0, 0, "missing operand");
        usage(EXIT_FAILURE);
    }
    for (; optind < argc; optind++) {
        const char *result = argv[optind];
        size_t length = dir_len(result);
        if (!length) {
            result = ".";
            length = 1;
        }
        fwrite(result, 1, length, stdout);
        putchar(zero ? '\0' : '\n');
    }
    return EXIT_SUCCESS;
}
