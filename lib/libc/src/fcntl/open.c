#include "../internal/narc.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>

int open(const char *path, int flags, ...) {
    const int known = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_DIRECTORY;
    if (flags & ~known) {
        errno = EINVAL;
        return -1;
    }
    uint32_t native = 0;
    switch (flags & O_ACCMODE) {
    case O_RDONLY: native |= NARC_OPEN_READ; break;
    case O_WRONLY: native |= NARC_OPEN_WRITE; break;
    case O_RDWR:   native |= NARC_OPEN_READ | NARC_OPEN_WRITE; break;
    default:
        errno = EINVAL;
        return -1;
    }
    if (flags & O_CREAT) native |= NARC_OPEN_CREATE;
    if (flags & O_EXCL) native |= NARC_OPEN_EXCLUSIVE;
    if (flags & O_TRUNC) native |= NARC_OPEN_TRUNCATE;
    if (flags & O_APPEND) native |= NARC_OPEN_APPEND;
    if (flags & O_DIRECTORY) native |= NARC_OPEN_DIRECTORY;
    return (int)__libc_result(narc_open(path, strlen(path), native));
}
