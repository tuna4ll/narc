#include "../internal/syscall.h"

narc_result_t narc_fs_info(const char *path, size_t path_length, narc_fs_info_t *info) {
    return __narc_call3(NARC_SYS_FS_INFO, (uint64_t)(uintptr_t)path, (uint64_t)path_length,
                        (uint64_t)(uintptr_t)info);
}

narc_result_t narc_file_fs_info(int fd, narc_fs_info_t *info) {
    return __narc_call3(NARC_SYS_FILE_FS_INFO, (uint64_t)(int64_t)fd, (uint64_t)(uintptr_t)info,
                        0);
}
