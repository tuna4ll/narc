#include "../internal/syscall.h"

narc_result_t narc_sync(int fd) {
    return __narc_call3(NARC_SYS_SYNC, (uint64_t)(int64_t)fd, 0, 0);
}
