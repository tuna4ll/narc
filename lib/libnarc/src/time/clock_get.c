#include "../internal/syscall.h"

narc_result_t narc_clock_get(uint32_t clock) {
    return __narc_call3(NARC_SYS_CLOCK_GET, clock, 0, 0);
}
