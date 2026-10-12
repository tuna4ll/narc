#include "../internal/syscall.h"

narc_result_t narc_fcntl(int fd, uint32_t command, uint64_t argument) {
    return __narc_call3(NARC_SYS_FCNTL, (uint64_t)(uint32_t)fd, command, argument);
}
