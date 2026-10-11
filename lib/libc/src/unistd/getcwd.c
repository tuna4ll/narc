#include "../internal/narc.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define PATH_LIMIT 4096

char *getcwd(char *buffer, size_t size) {
    char *target = buffer;
    if (!target) {
        if (!size) size = PATH_LIMIT;
        target = malloc(size);
        if (!target) return 0;
    } else if (!size) {
        errno = EINVAL;
        return 0;
    }
    if (__libc_result(narc_getcwd(target, size)) < 0) {
        if (!buffer) free(target);
        return 0;
    }
    return target;
}
