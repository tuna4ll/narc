#include "../internal/narc.h"
#include <signal.h>

int sigpending(sigset_t *set) {
    narc_result_t result = narc_sigpending();
    if (result.status != NARC_OK) return (int)__libc_result(result);
    *set = (sigset_t)result.value;
    return 0;
}
