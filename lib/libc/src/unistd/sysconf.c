#include <errno.h>
#include <limits.h>
#include <unistd.h>

long sysconf(int name) {
    switch (name) {
    case _SC_ARG_MAX: return 131072;
    case _SC_CHILD_MAX: return 1024;
    case _SC_CLK_TCK: return 100;
    case _SC_NGROUPS_MAX: return 32;
    case _SC_OPEN_MAX: return 256;
    case _SC_PAGESIZE: return 4096;
    case _SC_NPROCESSORS_CONF: case _SC_NPROCESSORS_ONLN: return 1;
    case _SC_LOGIN_NAME_MAX: return 256;
    case _SC_HOST_NAME_MAX: return 64;
    case _SC_LINE_MAX: return 2048;
    }
    errno = EINVAL;
    return -1;
}

int getpagesize(void) {
    return 4096;
}
