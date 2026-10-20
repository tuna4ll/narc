#include "../internal/attr.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int truncate(const char *path, off_t length) {
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    narc_file_attr_t attr = { .mask = NARC_ATTR_SIZE, .size = (uint64_t)length };
    return __set_attr_path(AT_FDCWD, path, &attr, 0);
}

int ftruncate(int fd, off_t length) {
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    narc_file_attr_t attr = { .mask = NARC_ATTR_SIZE, .size = (uint64_t)length };
    return __set_attr_fd(fd, &attr);
}
