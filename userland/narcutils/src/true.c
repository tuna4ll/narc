#include <narcutils/system.h>

#define PROGRAM_NAME "true"

void usage(int status) {
    printf("Usage: %s [ignored command line arguments]\n"
           "  or:  %s OPTION\n",
           program_name, program_name);
    fputs("Exit with a status code indicating success.\n\n", stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    printf("\nYour shell may have its own version of %s, which usually supersedes\n"
           "the version described here.  Please refer to your shell's documentation\n"
           "for details about the options it supports.\n",
           PROGRAM_NAME);
    exit(status);
}

int main(int argc, char **argv) {
    if (argc == 2) {
        initialize_main(&argc, &argv);
        if (STREQ(argv[1], "--help")) usage(EXIT_SUCCESS);
        if (STREQ(argv[1], "--version")) version_etc(PROGRAM_NAME);
    }
    return EXIT_SUCCESS;
}
