#include "../internal/narc.h"
#include <unistd.h>

pid_t getppid(void) {
    return (pid_t)__libc_result(narc_getppid());
}

int setpgid(pid_t pid, pid_t pgid) {
    return (int)__libc_result(narc_setpgid(pid, pgid));
}

pid_t getpgid(pid_t pid) {
    return (pid_t)__libc_result(narc_getpgid(pid));
}

pid_t getpgrp(void) {
    return getpgid(0);
}

pid_t setsid(void) {
    return (pid_t)__libc_result(narc_setsid());
}

pid_t getsid(pid_t pid) {
    return (pid_t)__libc_result(narc_getsid(pid));
}
