#include "../internal/narc.h"
#include <errno.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *status, int options) {
    if (options & ~WNOHANG) {
        errno = EINVAL;
        return -1;
    }
    return (pid_t)__libc_result(narc_wait(pid, status, (uint32_t)options));
}
