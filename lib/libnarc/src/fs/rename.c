#include "../internal/syscall.h"

narc_result_t narc_rename(const char *old_path, size_t old_length,
                          const char *new_path, size_t new_length) {
    return __narc_call4(NARC_SYS_RENAME, (uint64_t)(uintptr_t)old_path,
                        (uint64_t)old_length, (uint64_t)(uintptr_t)new_path,
                        (uint64_t)new_length);
}
