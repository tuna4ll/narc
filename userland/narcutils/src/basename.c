#include <narcutils/filenames.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "basename"

static const struct option long_options[] = {
    { "multiple", no_argument, 0, 'a' },
    { "suffix", required_argument, 0, 's' },
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
    printf("Usage: %s NAME [SUFFIX]\n"
           "  or:  %s OPTION... NAME...\n",
           program_name, program_name);
    fputs("Print NAME with any leading directory components removed.\n"
          "If specified, also remove a trailing SUFFIX.\n",
          stdout);
    emit_mandatory_arg_note();
    fputs("  -a, --multiple       support multiple arguments and treat each as a NAME\n"
          "  -s, --suffix=SUFFIX  remove a trailing SUFFIX; implies -a\n"
          "  -z, --zero           end each output line with NUL, not newline\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    printf("\nExamples:\n"
           "  %s /usr/bin/sort          -> \"sort\"\n"
           "  %s include/stdio.h .h     -> \"stdio\"\n"
           "  %s -s .h include/stdio.h  -> \"stdio\"\n"
           "  %s -a any/str1 any/str2   -> \"str1\" followed by \"str2\"\n",
           program_name, program_name, program_name, program_name);
    exit(status);
}

static void remove_suffix(char *name, const char *suffix) {
    size_t name_length = strlen(name), suffix_length = strlen(suffix);
    if (suffix_length >= name_length) return;
    if (!strcmp(name + name_length - suffix_length, suffix)) name[name_length - suffix_length] = 0;
}

static void perform(const char *string, const char *suffix, bool zero) {
    char *name = base_name(string);
    if (suffix && name[0] != '/' && !STREQ(name, "/")) remove_suffix(name, suffix);
    fputs(name, stdout);
    putchar(zero ? '\0' : '\n');
    free(name);
}

int main(int argc, char **argv) {
    bool multiple = false, zero = false;
    const char *suffix = 0;
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "+as:z", long_options, 0)) != -1) {
        switch (c) {
        case 's': suffix = optarg; /* fallthrough */
        case 'a': multiple = true; break;
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
    if (!multiple && optind + 2 < argc) {
        error(0, 0, "extra operand %s", quote(argv[optind + 2]));
        usage(EXIT_FAILURE);
    }
    if (multiple) {
        for (; optind < argc; optind++) perform(argv[optind], suffix, zero);
    } else {
        perform(argv[optind], optind + 2 == argc ? argv[optind + 1] : 0, zero);
    }
    return EXIT_SUCCESS;
}
