#include "../internal/syscall.h"

narc_result_t narc_read_dir(int fd, narc_dir_entry_t *entry) {
    return __narc_call3(NARC_SYS_READ_DIR, (uint64_t)(uint32_t)fd,
                        (uint64_t)(uintptr_t)entry, 0);
}
