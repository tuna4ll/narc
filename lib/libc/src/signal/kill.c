#include "../internal/narc.h"
#include <signal.h>
#include <unistd.h>

int kill(pid_t pid, int signal) {
    return (int)__libc_result(narc_kill(pid, signal));
}

int raise(int signal) {
    return kill(getpid(), signal);
}
