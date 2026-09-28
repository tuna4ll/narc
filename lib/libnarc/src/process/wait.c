#include "../internal/syscall.h"

narc_result_t narc_wait(int pid, int *status, uint32_t options) {
    return __narc_call3(NARC_SYS_WAIT, (uint64_t)(int64_t)pid,
                        (uint64_t)(uintptr_t)status, options);
}
