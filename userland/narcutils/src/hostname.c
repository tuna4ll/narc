#include <narcutils/quote.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "hostname"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [NAME]\n"
           "  or:  %s OPTION\n",
           program_name, program_name);
    fputs("Print or set the hostname of the current system.\n\n", stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    if (optind + 1 < argc) {
        error(0, 0, "extra operand %s", quote(argv[optind + 1]));
        usage(EXIT_FAILURE);
    }
    if (optind + 1 == argc) {
        const char *name = argv[optind];
        if (sethostname(name, strlen(name)) != 0)
            die(EXIT_FAILURE, errno, "cannot set name to %s", quote(name));
        return EXIT_SUCCESS;
    }
    char name[256];
    if (gethostname(name, sizeof(name)) != 0) die(EXIT_FAILURE, errno, "cannot determine hostname");
    name[sizeof(name) - 1] = 0;
    puts(name);
    return EXIT_SUCCESS;
}
