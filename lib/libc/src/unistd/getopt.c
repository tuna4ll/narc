#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int optind = 1, opterr = 1, optopt;

static int position;

int getopt(int argc, char *const argv[], const char *options) {
    if (optind == 0) {
        optind = 1;
        position = 0;
    }
    optarg = 0;
    if (optind >= argc || !argv[optind]) return -1;
    const char *argument = argv[optind];
    if (!position) {
        if (argument[0] != '-' || !argument[1]) return -1;
        if (argument[1] == '-' && !argument[2]) {
            optind++;
            return -1;
        }
        position = 1;
    }
    int silent = options[0] == ':';
    char c = argument[position++];
    const char *match = c != ':' ? strchr(options, c) : 0;
    if (!match) {
        optopt = c;
        if (!argument[position]) {
            optind++;
            position = 0;
        }
        if (opterr && !silent) fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
        return '?';
    }
    if (match[1] == ':') {
        if (argument[position]) {
            optarg = (char *)argument + position;
            optind++;
        } else if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            optind += 2;
        } else {
            optopt = c;
            optind++;
            position = 0;
            if (silent) return ':';
            if (opterr) fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
            return '?';
        }
        position = 0;
        return c;
    }
    if (!argument[position]) {
        optind++;
        position = 0;
    }
    return c;
}
