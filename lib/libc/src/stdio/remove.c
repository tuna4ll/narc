#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int remove(const char *path) {
    if (unlink(path) == 0) return 0;
    if (errno != EISDIR) return -1;
    return rmdir(path);
}

void perror(const char *prefix) {
    int saved = errno;
    if (prefix && *prefix) fprintf(stderr, "%s: ", prefix);
    fprintf(stderr, "%s\n", strerror(saved));
}
