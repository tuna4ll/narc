#include <narcutils/system.h>

#define PROGRAM_NAME "pwd"

static const struct option long_options[] = {
    { "logical", no_argument, 0, 'L' },
    { "physical", no_argument, 0, 'P' },
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
    fputs("Print the full filename of the current working directory.\n\n"
          "  -L, --logical   use PWD from environment, even if it contains symlinks\n"
          "  -P, --physical  resolve all symlinks\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    fputs("\nIf no option is specified, -P is assumed.\n", stdout);
    printf("\nYour shell may have its own version of %s, which usually supersedes\n"
           "the version described here.  Please refer to your shell's documentation\n"
           "for details about the options it supports.\n",
           PROGRAM_NAME);
    exit(status);
}

static const char *logical_getcwd(void) {
    const char *wd = getenv("PWD");
    if (!wd || wd[0] != '/') return 0;
    for (const char *p = wd; (p = strstr(p, "/.")); p++)
        if (!p[2] || p[2] == '/' || (p[2] == '.' && (!p[3] || p[3] == '/'))) return 0;
    struct stat a, b;
    if (stat(wd, &a) == 0 && stat(".", &b) == 0 && a.st_ino == b.st_ino && a.st_dev == b.st_dev)
        return wd;
    return 0;
}

int main(int argc, char **argv) {
    bool logical = getenv("POSIXLY_CORRECT") != 0;
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "LP", long_options, 0)) != -1) {
        switch (c) {
        case 'L': logical = true; break;
        case 'P': logical = false; break;
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
        default: usage(EXIT_FAILURE);
        }
    }
    if (optind < argc) error(0, 0, "ignoring non-option arguments");
    if (logical) {
        const char *wd = logical_getcwd();
        if (wd) {
            puts(wd);
            return EXIT_SUCCESS;
        }
    }
    char *wd = getcwd(0, 0);
    if (!wd) die(EXIT_FAILURE, errno, "cannot determine current directory");
    puts(wd);
    free(wd);
    return EXIT_SUCCESS;
}
