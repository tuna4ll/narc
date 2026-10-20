#include "attr.h"
#include "narc.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>

int __set_attr_path(int dirfd, const char *path, const narc_file_attr_t *attr, int nofollow) {
    if (!path || !*path) {
        errno = ENOENT;
        return -1;
    }
    if (dirfd != AT_FDCWD && path[0] != '/') {
        errno = ENOSYS;
        return -1;
    }
    uint32_t flags = nofollow ? NARC_STAT_NOFOLLOW : 0;
    return __libc_result(narc_set_attr(path, strlen(path), attr, flags)) < 0 ? -1 : 0;
}

int __set_attr_fd(int fd, const narc_file_attr_t *attr) {
    return __libc_result(narc_file_set_attr(fd, attr)) < 0 ? -1 : 0;
}
