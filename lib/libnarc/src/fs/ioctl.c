#include "../internal/syscall.h"

narc_result_t narc_ioctl(int fd, uint32_t request, uint64_t argument) {
    return __narc_call3(NARC_SYS_IOCTL, (uint64_t)(uint32_t)fd, request, argument);
}
