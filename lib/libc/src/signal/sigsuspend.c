#include "../internal/narc.h"
#include <signal.h>

int sigsuspend(const sigset_t *mask) {
    return (int)__libc_result(narc_sigsuspend(*mask));
}
