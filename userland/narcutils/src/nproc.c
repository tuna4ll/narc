#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <narcutils/xstrtol.h>

#define PROGRAM_NAME "nproc"

enum { ALL_OPTION = CHAR_MAX + 1, IGNORE_OPTION };

static const struct option long_options[] = {
    { "all", no_argument, 0, ALL_OPTION },
    { "ignore", required_argument, 0, IGNORE_OPTION },
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
    fputs("Print the number of processing units available to the current process,\n"
          "which may be less than the number of online processors\n\n"
          "      --all       print the number of installed processors\n"
          "      --ignore=N  if possible, exclude N processing units\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    uintmax_t ignore = 0;
    bool all = false;
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "", long_options, 0)) != -1) {
        switch (c) {
        case ALL_OPTION: all = true; break;
        case IGNORE_OPTION: {
            strtol_error status = xstrtoumax(optarg, 0, 10, &ignore, "");
            if (status != LONGINT_OK) die(EXIT_FAILURE, 0, "invalid number: %s", quote(optarg));
            break;
        }
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
        default: usage(EXIT_FAILURE);
        }
    }
    if (argc != optind) {
        error(0, 0, "extra operand %s", quote(argv[optind]));
        usage(EXIT_FAILURE);
    }
    const char *limit = getenv("OMP_NUM_THREADS");
    long processors = sysconf(all ? _SC_NPROCESSORS_CONF : _SC_NPROCESSORS_ONLN);
    if (!all && limit && *limit) {
        char *end;
        long value = strtol(limit, &end, 10);
        if (!*end && value > 0) processors = value;
    }
    if (processors < 1) processors = 1;
    uintmax_t result = (uintmax_t)processors;
    result = ignore < result ? result - ignore : 1;
    printf("%ju\n", result);
    return EXIT_SUCCESS;
}
