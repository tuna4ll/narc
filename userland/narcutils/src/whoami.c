#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <pwd.h>

#define PROGRAM_NAME "whoami"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION]...\n", program_name);
    fputs("Print the user name associated with the current effective user ID.\n"
          "Same as id -un.\n\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    if (optind != argc) {
        error(0, 0, "extra operand %s", quote(argv[optind]));
        usage(EXIT_FAILURE);
    }
    errno = 0;
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) die(EXIT_FAILURE, errno, "cannot find name for user ID %lu", (unsigned long)uid);
    puts(pw->pw_name);
    return EXIT_SUCCESS;
}
