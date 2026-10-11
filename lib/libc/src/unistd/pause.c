#include <signal.h>
#include <unistd.h>

int pause(void) {
    sigset_t mask;
    sigprocmask(SIG_BLOCK, 0, &mask);
    return sigsuspend(&mask);
}
