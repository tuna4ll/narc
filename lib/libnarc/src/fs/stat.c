#include "../internal/syscall.h"

narc_result_t narc_stat(const char *path, size_t path_length, narc_file_info_t *info,
                        uint32_t flags) {
    return __narc_call4(NARC_SYS_STAT, (uint64_t)(uintptr_t)path, (uint64_t)path_length,
                        (uint64_t)(uintptr_t)info, flags);
}
