#include "../internal/narc.h"
#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

_Static_assert(sizeof(struct utsname) == sizeof(narc_utsname_t), "utsname layout");

int uname(struct utsname *info) {
    return (int)__libc_result(narc_uname((narc_utsname_t *)info));
}

int gethostname(char *name, size_t length) {
    struct utsname info;
    if (uname(&info) != 0) return -1;
    size_t size = strlen(info.nodename) + 1;
    if (size > length) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(name, info.nodename, size);
    return 0;
}

int sethostname(const char *name, size_t length) {
    return (int)__libc_result(narc_sethostname(name, length));
}
