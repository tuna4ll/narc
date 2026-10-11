#include "../internal/narc.h"
#include <errno.h>
#include <signal.h>

int sigaction(int signal, const struct sigaction *action, struct sigaction *old_action) {
    narc_sigaction_t native, previous;
    if (action) {
        native.handler = (uint64_t)(uintptr_t)action->sa_handler;
        native.flags = (uint32_t)action->sa_flags;
        native.mask = action->sa_mask;
        native.restorer = (uint64_t)(uintptr_t)narc_signal_return;
    }
    if (__libc_result(narc_sigaction(signal, action ? &native : 0, &previous)) < 0) return -1;
    if (old_action) {
        old_action->sa_handler = (void (*)(int))(uintptr_t)previous.handler;
        old_action->sa_flags = (int)previous.flags;
        old_action->sa_mask = previous.mask;
        old_action->sa_restorer = (void (*)(void))(uintptr_t)previous.restorer;
    }
    return 0;
}
