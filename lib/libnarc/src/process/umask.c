#include "../internal/syscall.h"

narc_result_t narc_umask(uint32_t mask) {
    return __narc_call3(NARC_SYS_UMASK, mask, 0, 0);
}
