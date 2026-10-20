#include "../internal/attr.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static void build(narc_file_attr_t *attr, uid_t owner, gid_t group) {
    attr->mask = 0;
    if (owner != (uid_t)-1) {
        attr->mask |= NARC_ATTR_UID;
        attr->uid = owner;
    }
    if (group != (gid_t)-1) {
        attr->mask |= NARC_ATTR_GID;
        attr->gid = group;
    }
}

int chown(const char *path, uid_t owner, gid_t group) {
    narc_file_attr_t attr = { 0 };
    build(&attr, owner, group);
    return __set_attr_path(AT_FDCWD, path, &attr, 0);
}

int lchown(const char *path, uid_t owner, gid_t group) {
    narc_file_attr_t attr = { 0 };
    build(&attr, owner, group);
    return __set_attr_path(AT_FDCWD, path, &attr, 1);
}

int fchown(int fd, uid_t owner, gid_t group) {
    narc_file_attr_t attr = { 0 };
    build(&attr, owner, group);
    return __set_attr_fd(fd, &attr);
}

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags) {
    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        errno = EINVAL;
        return -1;
    }
    narc_file_attr_t attr = { 0 };
    build(&attr, owner, group);
    return __set_attr_path(dirfd, path, &attr, flags & AT_SYMLINK_NOFOLLOW);
}
