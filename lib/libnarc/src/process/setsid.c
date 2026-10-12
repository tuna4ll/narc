#include "../internal/syscall.h"

narc_result_t narc_setsid(void) {
    return __narc_call3(NARC_SYS_SETSID, 0, 0, 0);
}
