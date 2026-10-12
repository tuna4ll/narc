#include "../internal/narc.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int pipe2(int fds[2], int flags) {
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
        errno = EINVAL;
        return -1;
    }
    uint32_t native = 0;
    if (flags & O_CLOEXEC) native |= NARC_OPEN_CLOEXEC;
    if (flags & O_NONBLOCK) native |= NARC_OPEN_NONBLOCK;
    int32_t pair[2];
    if (__libc_result(narc_pipe(pair, native)) < 0) return -1;
    fds[0] = pair[0];
    fds[1] = pair[1];
    return 0;
}

int pipe(int fds[2]) {
    return pipe2(fds, 0);
}
