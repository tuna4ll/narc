#include "../internal/syscall.h"

narc_result_t narc_sigaction(int signal, const narc_sigaction_t *action,
                             narc_sigaction_t *old_action) {
    return __narc_call3(NARC_SYS_SIGACTION, (uint64_t)(int64_t)signal,
                        (uint64_t)(uintptr_t)action, (uint64_t)(uintptr_t)old_action);
}
