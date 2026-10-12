#include "../internal/narc.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

static int to_posix(uint32_t native) {
    int flags = (native & NARC_OPEN_READ) && (native & NARC_OPEN_WRITE) ? O_RDWR :
                (native & NARC_OPEN_WRITE) ? O_WRONLY : O_RDONLY;
    if (native & NARC_OPEN_APPEND) flags |= O_APPEND;
    if (native & NARC_OPEN_NONBLOCK) flags |= O_NONBLOCK;
    return flags;
}

int fcntl(int fd, int command, ...) {
    va_list arguments;
    va_start(arguments, command);
    long argument = va_arg(arguments, long);
    va_end(arguments);
    switch (command) {
    case F_DUPFD:
        return (int)__libc_result(narc_fcntl(fd, NARC_FCNTL_DUPFD, (uint64_t)argument));
    case F_DUPFD_CLOEXEC:
        return (int)__libc_result(narc_fcntl(fd, NARC_FCNTL_DUPFD_CLOEXEC, (uint64_t)argument));
    case F_GETFD: {
        long flags = __libc_result(narc_fcntl(fd, NARC_FCNTL_GETFD, 0));
        return flags < 0 ? -1 : (flags & NARC_FD_CLOEXEC ? FD_CLOEXEC : 0);
    }
    case F_SETFD:
        return (int)__libc_result(narc_fcntl(fd, NARC_FCNTL_SETFD,
                                             argument & FD_CLOEXEC ? NARC_FD_CLOEXEC : 0));
    case F_GETFL: {
        long flags = __libc_result(narc_fcntl(fd, NARC_FCNTL_GETFL, 0));
        return flags < 0 ? -1 : to_posix((uint32_t)flags);
    }
    case F_SETFL: {
        uint64_t native = 0;
        if (argument & O_APPEND) native |= NARC_OPEN_APPEND;
        if (argument & O_NONBLOCK) native |= NARC_OPEN_NONBLOCK;
        return (int)__libc_result(narc_fcntl(fd, NARC_FCNTL_SETFL, native));
    }
    default:
        errno = EINVAL;
        return -1;
    }
}
