#include "../internal/narc.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>

int kill(pid_t pid, int signal) {
    return (int)__libc_result(narc_kill(pid, signal));
}

int killpg(pid_t group, int signal) {
    if (group <= 1) {
        errno = EINVAL;
        return -1;
    }
    return kill(-group, signal);
}

int raise(int signal) {
    return kill(getpid(), signal);
}
