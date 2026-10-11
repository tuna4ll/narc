#include "../internal/syscall.h"

narc_result_t narc_getcwd(char *buffer, size_t size) {
    return __narc_call3(NARC_SYS_GETCWD, (uint64_t)(uintptr_t)buffer, (uint64_t)size, 0);
}
