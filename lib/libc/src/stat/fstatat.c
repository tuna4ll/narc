#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

int fstatat(int dirfd, const char *restrict path, struct stat *restrict buffer, int flags) {
    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        errno = EINVAL;
        return -1;
    }
    if (dirfd != AT_FDCWD && path[0] != '/') {
        errno = ENOSYS;
        return -1;
    }
    return (flags & AT_SYMLINK_NOFOLLOW) ? lstat(path, buffer) : stat(path, buffer);
}
