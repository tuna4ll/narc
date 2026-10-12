#include "../internal/syscall.h"

narc_result_t narc_open(const char *path, size_t path_length, uint32_t flags, uint32_t mode) {
    return __narc_call4(NARC_SYS_OPEN, (uint64_t)(uintptr_t)path,
                        (uint64_t)path_length, flags, mode);
}
