#include "../internal/syscall.h"

narc_result_t narc_set_attr(const char *path, size_t path_length, const narc_file_attr_t *attr,
                            uint32_t flags) {
    return __narc_call4(NARC_SYS_SET_ATTR, (uint64_t)(uintptr_t)path, (uint64_t)path_length,
                        (uint64_t)(uintptr_t)attr, flags);
}

narc_result_t narc_file_set_attr(int fd, const narc_file_attr_t *attr) {
    return __narc_call3(NARC_SYS_FILE_SET_ATTR, (uint64_t)(int64_t)fd, (uint64_t)(uintptr_t)attr,
                        0);
}
