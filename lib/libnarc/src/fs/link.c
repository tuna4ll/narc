#include "../internal/syscall.h"

narc_result_t narc_symlink(const char *target, const char *path) {
    return __narc_call3(NARC_SYS_SYMLINK, (uint64_t)(uintptr_t)target, (uint64_t)(uintptr_t)path, 0);
}

narc_result_t narc_link(const char *old_path, const char *new_path) {
    return __narc_call3(NARC_SYS_LINK, (uint64_t)(uintptr_t)old_path,
                        (uint64_t)(uintptr_t)new_path, 0);
}

narc_result_t narc_readlink(const char *path, char *buffer, size_t size) {
    return __narc_call3(NARC_SYS_READLINK, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)buffer,
                        (uint64_t)size);
}
