#include "../internal/syscall.h"

narc_result_t narc_getsid(int pid) {
    return __narc_call3(NARC_SYS_GETSID, (uint64_t)(int64_t)pid, 0, 0);
}
