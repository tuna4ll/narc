#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *realpath(const char *restrict path, char *restrict resolved) {
    char *buffer = resolved ? resolved : malloc(PATH_MAX);
    if (!buffer) return 0;
    char work[PATH_MAX];
    size_t length = 0;
    if (path[0] != '/') {
        if (!getcwd(work, sizeof(work))) goto fail;
        length = strlen(work);
        if (length == 1) length = 0;
    }
    work[length] = 0;
    for (const char *cursor = path; *cursor;) {
        while (*cursor == '/') cursor++;
        const char *start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        size_t part = (size_t)(cursor - start);
        if (!part || (part == 1 && start[0] == '.')) continue;
        if (part == 2 && start[0] == '.' && start[1] == '.') {
            while (length && work[length - 1] != '/') length--;
            if (length) length--;
            work[length] = 0;
            continue;
        }
        if (length + part + 2 > sizeof(work)) {
            errno = ENAMETOOLONG;
            goto fail;
        }
        work[length++] = '/';
        memcpy(work + length, start, part);
        length += part;
        work[length] = 0;
    }
    if (!length) {
        work[0] = '/';
        work[1] = 0;
    }
    struct stat st;
    if (stat(work, &st) != 0) goto fail;
    strcpy(buffer, work);
    return buffer;
fail:
    if (!resolved) free(buffer);
    return 0;
}
