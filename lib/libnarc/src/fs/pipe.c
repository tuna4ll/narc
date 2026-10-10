#include "../internal/syscall.h"

narc_result_t narc_pipe(int32_t fds[2]) {
    return __narc_call3(NARC_SYS_PIPE, (uint64_t)(uintptr_t)fds, 0, 0);
}
