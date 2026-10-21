#include <narcutils/quote.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "link"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s FILE1 FILE2\n"
           "  or:  %s OPTION\n",
           program_name, program_name);
    fputs("Call the link function to create a link named FILE2 to an existing FILE1.\n\n", stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    if (argc < optind + 2) {
        if (argc < optind + 1) error(0, 0, "missing operand");
        else error(0, 0, "missing operand after %s", quote(argv[optind]));
        usage(EXIT_FAILURE);
    }
    if (optind + 2 < argc) {
        error(0, 0, "extra operand %s", quote(argv[optind + 2]));
        usage(EXIT_FAILURE);
    }
    if (link(argv[optind], argv[optind + 1]) != 0)
        die(EXIT_FAILURE, errno, "cannot create link %s to %s", quoteaf_n(0, argv[optind + 1]),
            quoteaf_n(1, argv[optind]));
    return EXIT_SUCCESS;
}
