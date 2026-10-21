#include <narcutils/quote.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "unlink"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s FILE\n"
           "  or:  %s OPTION\n",
           program_name, program_name);
    fputs("Call the unlink function to remove the specified FILE.\n\n", stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    if (argc < optind + 1) {
        error(0, 0, "missing operand");
        usage(EXIT_FAILURE);
    }
    if (optind + 1 < argc) {
        error(0, 0, "extra operand %s", quote(argv[optind + 1]));
        usage(EXIT_FAILURE);
    }
    if (unlink(argv[optind]) != 0) die(EXIT_FAILURE, errno, "cannot unlink %s", quoteaf(argv[optind]));
    return EXIT_SUCCESS;
}
