#include "../internal/syscall.h"

narc_result_t narc_uname(narc_utsname_t *info) {
    return __narc_call3(NARC_SYS_UNAME, (uint64_t)(uintptr_t)info, 0, 0);
}
