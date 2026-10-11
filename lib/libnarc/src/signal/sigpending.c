#include "../internal/syscall.h"

narc_result_t narc_sigpending(void) {
    return __narc_call3(NARC_SYS_SIGPENDING, 0, 0, 0);
}
