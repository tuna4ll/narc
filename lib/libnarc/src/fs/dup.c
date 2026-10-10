#include "../internal/syscall.h"

narc_result_t narc_dup(int fd, int minimum) {
    return __narc_call3(NARC_SYS_DUP, (uint64_t)(uint32_t)fd, (uint64_t)(uint32_t)minimum, 0);
}

narc_result_t narc_dup2(int fd, int target) {
    return __narc_call3(NARC_SYS_DUP2, (uint64_t)(uint32_t)fd, (uint64_t)(uint32_t)target, 0);
}
