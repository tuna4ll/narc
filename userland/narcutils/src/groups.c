#include <narcutils/group_list.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <pwd.h>

#define PROGRAM_NAME "groups"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION]... [USERNAME]...\n", program_name);
    fputs("Print group memberships for each USERNAME or, if no USERNAME is specified, for\n"
          "the current process (which may differ if the groups database has changed).\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    bool ok = true;
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    if (optind == argc) {
        if (!print_group_list(0, getuid(), getgid(), getegid(), true, ' ')) ok = false;
        putchar('\n');
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    for (; optind < argc; optind++) {
        struct passwd *pw = getpwnam(argv[optind]);
        if (!pw) {
            error(0, 0, "%s: no such user", quote(argv[optind]));
            ok = false;
            continue;
        }
        uid_t uid = pw->pw_uid;
        gid_t gid = pw->pw_gid;
        printf("%s : ", argv[optind]);
        if (!print_group_list(argv[optind], uid, gid, gid, true, ' ')) ok = false;
        putchar('\n');
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
