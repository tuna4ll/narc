#include <errno.h>
#include <signal.h>

static int valid(int signal) {
    if (signal > 0 && signal < NSIG) return 1;
    errno = EINVAL;
    return 0;
}

int sigemptyset(sigset_t *set) {
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    *set = ((sigset_t)1 << NSIG) - 2;
    return 0;
}

int sigaddset(sigset_t *set, int signal) {
    if (!valid(signal)) return -1;
    *set |= (sigset_t)1 << signal;
    return 0;
}

int sigdelset(sigset_t *set, int signal) {
    if (!valid(signal)) return -1;
    *set &= ~((sigset_t)1 << signal);
    return 0;
}

int sigismember(const sigset_t *set, int signal) {
    if (!valid(signal)) return -1;
    return (*set >> signal) & 1;
}
