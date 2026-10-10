#include "../internal/narc.h"
#include <unistd.h>

int pipe(int fds[2]) {
    int32_t pair[2];
    if (__libc_result(narc_pipe(pair)) < 0) return -1;
    fds[0] = pair[0];
    fds[1] = pair[1];
    return 0;
}
