#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

_Noreturn void abort(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGABRT);
    sigprocmask(SIG_UNBLOCK, &set, 0);
    raise(SIGABRT);
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
    _exit(127);
}
