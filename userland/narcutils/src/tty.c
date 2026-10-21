#include <narcutils/quote.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "tty"

enum { TTY_FAILURE = 2, TTY_WRITE_ERROR = 3 };

static const struct option long_options[] = {
    { "silent", no_argument, 0, 's' },
    { "quiet", no_argument, 0, 's' },
    GETOPT_HELP_OPTION_DECL,
    GETOPT_VERSION_OPTION_DECL,
    { 0, 0, 0, 0 },
};

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION]...\n", program_name);
    fputs("Print the file name of the terminal connected to standard input.\n\n"
          "  -s, --silent, --quiet   print nothing, only return an exit status\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    bool silent = false;
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "s", long_options, 0)) != -1) {
        switch (c) {
        case 's': silent = true; break;
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
        default: usage(TTY_FAILURE);
        }
    }
    if (optind < argc) {
        error(0, 0, "extra operand %s", quote(argv[optind]));
        usage(TTY_FAILURE);
    }
    errno = ENOENT;
    if (silent) return isatty(STDIN_FILENO) ? EXIT_SUCCESS : EXIT_FAILURE;
    int status = EXIT_SUCCESS;
    const char *name = ttyname(STDIN_FILENO);
    if (!name) {
        if (errno != ENOTTY) error(0, errno, "ttyname error");
        name = "not a tty";
        status = EXIT_FAILURE;
    }
    puts(name);
    return status;
}
