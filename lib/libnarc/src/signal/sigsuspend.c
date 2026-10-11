#include "../internal/syscall.h"

narc_result_t narc_sigsuspend(uint64_t mask) {
    return __narc_call3(NARC_SYS_SIGSUSPEND, mask, 0, 0);
}
