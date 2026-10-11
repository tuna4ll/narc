#include "../internal/syscall.h"

narc_result_t narc_sleep(uint64_t nanoseconds) {
    return __narc_call3(NARC_SYS_SLEEP, nanoseconds, 0, 0);
}
