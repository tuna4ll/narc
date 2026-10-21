#include <narcutils/quote.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "logname"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION]\n", program_name);
    fputs("Print the user's login name.\n\n", stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    if (optind < argc) {
        error(0, 0, "extra operand %s", quote(argv[optind]));
        usage(EXIT_FAILURE);
    }
    const char *name = getlogin();
    if (!name) die(EXIT_FAILURE, 0, "no login name");
    puts(name);
    return EXIT_SUCCESS;
}
