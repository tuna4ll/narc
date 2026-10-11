#include "../internal/syscall.h"

narc_result_t narc_kill(int pid, int signal) {
    return __narc_call3(NARC_SYS_KILL, (uint64_t)(int64_t)pid, (uint64_t)(int64_t)signal, 0);
}
