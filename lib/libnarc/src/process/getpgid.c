#include "../internal/syscall.h"

narc_result_t narc_getpgid(int pid) {
    return __narc_call3(NARC_SYS_GETPGID, (uint64_t)(int64_t)pid, 0, 0);
}
