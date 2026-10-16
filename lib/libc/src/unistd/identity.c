#include <errno.h>
#include <unistd.h>

uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }

int getgroups(int size, gid_t list[]) {
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }
    if (size) list[0] = 0;
    return 1;
}

int setuid(uid_t uid) {
    if (uid == 0) return 0;
    errno = EPERM;
    return -1;
}

int setgid(gid_t gid) {
    if (gid == 0) return 0;
    errno = EPERM;
    return -1;
}
