#include "../internal/syscall.h"

narc_result_t narc_sethostname(const char *name, size_t length) {
    return __narc_call3(NARC_SYS_SETHOSTNAME, (uint64_t)(uintptr_t)name, (uint64_t)length, 0);
}
