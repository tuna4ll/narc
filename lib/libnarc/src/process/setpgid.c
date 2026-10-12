#include "../internal/syscall.h"

narc_result_t narc_setpgid(int pid, int pgid) {
    return __narc_call3(NARC_SYS_SETPGID, (uint64_t)(int64_t)pid, (uint64_t)(int64_t)pgid, 0);
}
