#include "../internal/syscall.h"

narc_result_t narc_fchdir(int fd) {
    return __narc_call3(NARC_SYS_FCHDIR, (uint64_t)(uint32_t)fd, 0, 0);
}
