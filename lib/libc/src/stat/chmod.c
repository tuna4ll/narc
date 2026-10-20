#include "../internal/attr.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

int chmod(const char *path, mode_t mode) {
    narc_file_attr_t attr = { .mask = NARC_ATTR_MODE, .mode = mode & 07777 };
    return __set_attr_path(AT_FDCWD, path, &attr, 0);
}

int fchmod(int fd, mode_t mode) {
    narc_file_attr_t attr = { .mask = NARC_ATTR_MODE, .mode = mode & 07777 };
    return __set_attr_fd(fd, &attr);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        errno = EINVAL;
        return -1;
    }
    narc_file_attr_t attr = { .mask = NARC_ATTR_MODE, .mode = mode & 07777 };
    return __set_attr_path(dirfd, path, &attr, flags & AT_SYMLINK_NOFOLLOW);
}
