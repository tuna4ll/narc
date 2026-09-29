#include "../internal/syscall.h"

narc_result_t narc_mkdir(const char *path, size_t path_length, uint32_t mode) {
    return __narc_call3(NARC_SYS_MKDIR, (uint64_t)(uintptr_t)path,
                        (uint64_t)path_length, mode);
}
