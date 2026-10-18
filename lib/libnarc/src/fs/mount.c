#include "../internal/syscall.h"

narc_result_t narc_mount(const char *source, const char *target, const char *type,
                         uint32_t flags) {
    return __narc_call4(NARC_SYS_MOUNT, (uint64_t)(uintptr_t)source, (uint64_t)(uintptr_t)target,
                        (uint64_t)(uintptr_t)type, flags);
}

narc_result_t narc_umount(const char *target) {
    return __narc_call3(NARC_SYS_UMOUNT, (uint64_t)(uintptr_t)target, 0, 0);
}
