#include "../internal/syscall.h"

narc_result_t narc_rmdir(const char *path, size_t path_length) {
    return __narc_call3(NARC_SYS_RMDIR, (uint64_t)(uintptr_t)path,
                        (uint64_t)path_length, 0);
}
