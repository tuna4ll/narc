#include "../internal/syscall.h"

narc_result_t narc_sigmask(uint32_t how, uint64_t set) {
    return __narc_call3(NARC_SYS_SIGMASK, how, set, 0);
}
