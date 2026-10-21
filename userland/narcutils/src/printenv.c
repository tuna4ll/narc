#include <narcutils/system.h>

#define PROGRAM_NAME "printenv"

extern char **environ;

static const struct option long_options[] = {
    { "null", no_argument, 0, '0' },
    GETOPT_HELP_OPTION_DECL,
    GETOPT_VERSION_OPTION_DECL,
    { 0, 0, 0, 0 },
};

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION]... [VARIABLE]...\n", program_name);
    fputs("Print the values of the specified environment VARIABLE(s).\n"
          "If no VARIABLE is specified, print name and value pairs for them all.\n\n"
          "  -0, --null     end each output line with NUL, not newline\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    printf("\nYour shell may have its own version of %s, which usually supersedes\n"
           "the version described here.  Please refer to your shell's documentation\n"
           "for details about the options it supports.\n",
           PROGRAM_NAME);
    exit(status);
}

int main(int argc, char **argv) {
    char delimiter = '\n';
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "+iu:0", long_options, 0)) != -1) {
        switch (c) {
        case '0': delimiter = '\0'; break;
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
        default: usage(EXIT_TROUBLE);
        }
    }
    if (optind >= argc) {
        for (char **env = environ; *env; env++) printf("%s%c", *env, delimiter);
        return EXIT_SUCCESS;
    }
    int matches = 0;
    for (int i = optind; i < argc; i++) {
        if (strchr(argv[i], '=')) continue;
        size_t length = strlen(argv[i]);
        for (char **env = environ; *env; env++) {
            if (!strncmp(*env, argv[i], length) && (*env)[length] == '=') {
                printf("%s%c", *env + length + 1, delimiter);
                matches++;
                break;
            }
        }
    }
    return matches == argc - optind ? EXIT_SUCCESS : EXIT_FAILURE;
}
