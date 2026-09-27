#include "../internal/syscall.h"

narc_result_t narc_file_info(int fd, narc_file_info_t *info) {
    return __narc_call3(NARC_SYS_FILE_INFO, (uint64_t)(uint32_t)fd,
                        (uint64_t)(uintptr_t)info, 0);
}
