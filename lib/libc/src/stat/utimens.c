#include "../internal/attr.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>

static int build(narc_file_attr_t *attr, const struct timespec times[2]) {
    attr->mask = 0;
    if (!times) {
        attr->mask = NARC_ATTR_ATIME_NOW | NARC_ATTR_MTIME_NOW;
        return 0;
    }
    uint32_t now_bits[2] = { NARC_ATTR_ATIME_NOW, NARC_ATTR_MTIME_NOW };
    uint32_t set_bits[2] = { NARC_ATTR_ATIME, NARC_ATTR_MTIME };
    int64_t *slots[2] = { &attr->atime, &attr->mtime };
    for (int i = 0; i < 2; i++) {
        long nsec = times[i].tv_nsec;
        if (nsec == UTIME_OMIT) continue;
        if (nsec == UTIME_NOW) {
            attr->mask |= now_bits[i];
            continue;
        }
        if (nsec < 0 || nsec >= 1000000000l) {
            errno = EINVAL;
            return -1;
        }
        attr->mask |= set_bits[i];
        *slots[i] = times[i].tv_sec;
    }
    return 0;
}

int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags) {
    narc_file_attr_t attr = { 0 };
    if ((flags & ~AT_SYMLINK_NOFOLLOW) || build(&attr, times) != 0) {
        if (!errno) errno = EINVAL;
        return -1;
    }
    if (!attr.mask) return 0;
    return __set_attr_path(dirfd, path, &attr, flags & AT_SYMLINK_NOFOLLOW);
}

int futimens(int fd, const struct timespec times[2]) {
    narc_file_attr_t attr = { 0 };
    if (build(&attr, times) != 0) return -1;
    if (!attr.mask) return 0;
    return __set_attr_fd(fd, &attr);
}

int utimes(const char *path, const struct timeval times[2]) {
    if (!times) return utimensat(AT_FDCWD, path, 0, 0);
    struct timespec converted[2] = {
        { times[0].tv_sec, (long)times[0].tv_usec * 1000 },
        { times[1].tv_sec, (long)times[1].tv_usec * 1000 },
    };
    return utimensat(AT_FDCWD, path, converted, 0);
}

int utime(const char *path, const struct utimbuf *times) {
    if (!times) return utimensat(AT_FDCWD, path, 0, 0);
    struct timespec converted[2] = { { times->actime, 0 }, { times->modtime, 0 } };
    return utimensat(AT_FDCWD, path, converted, 0);
}
