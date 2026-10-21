#include <fcntl.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "sync"

static const struct option long_options[] = {
    { "data", no_argument, 0, 'd' },
    { "file-system", no_argument, 0, 'f' },
    GETOPT_HELP_OPTION_DECL,
    GETOPT_VERSION_OPTION_DECL,
    { 0, 0, 0, 0 },
};

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION] [FILE]...\n", program_name);
    fputs("Synchronize cached writes to persistent storage\n\n"
          "If one or more files are specified, sync only them,\n"
          "or their containing file systems.\n\n"
          "  -d, --data             sync only file data, no unneeded metadata\n"
          "  -f, --file-system      sync the file systems that contain the files\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

static bool sync_file(const char *name, bool data) {
    int fd = open(name, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        int saved = errno;
        fd = open(name, O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            error(0, saved, "error opening %s", quoteaf(name));
            return false;
        }
    }
    bool ok = true;
    if ((data ? fdatasync(fd) : fsync(fd)) != 0) {
        error(0, errno, "error syncing %s", quoteaf(name));
        ok = false;
    }
    if (close(fd) != 0) {
        error(0, errno, "failed to close %s", quoteaf(name));
        ok = false;
    }
    return ok;
}

int main(int argc, char **argv) {
    bool data = false, file_system = false;
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "df", long_options, 0)) != -1) {
        switch (c) {
        case 'd': data = true; break;
        case 'f': file_system = true; break;
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
        default: usage(EXIT_FAILURE);
        }
    }
    bool args = optind < argc;
    if (data && file_system) die(EXIT_FAILURE, 0, "cannot specify both --data and --file-system");
    if (!args && data) die(EXIT_FAILURE, 0, "--data needs at least one argument");
    if (!args || file_system) {
        sync();
        if (!args) return EXIT_SUCCESS;
    }
    bool ok = true;
    for (; optind < argc; optind++)
        if (!sync_file(argv[optind], data)) ok = false;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
