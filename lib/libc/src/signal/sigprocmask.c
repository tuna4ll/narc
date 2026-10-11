#include "../internal/narc.h"
#include <errno.h>
#include <signal.h>

int sigprocmask(int how, const sigset_t *set, sigset_t *old_set) {
    if (set && how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK) {
        errno = EINVAL;
        return -1;
    }
    narc_result_t result = set ? narc_sigmask((uint32_t)how, *set) : narc_sigmask(SIG_BLOCK, 0);
    if (result.status != NARC_OK) return (int)__libc_result(result);
    if (old_set) *old_set = (sigset_t)result.value;
    return 0;
}
