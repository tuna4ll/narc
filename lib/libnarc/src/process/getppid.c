#include "../internal/syscall.h"

narc_result_t narc_getppid(void) {
    return __narc_call3(NARC_SYS_GETPPID, 0, 0, 0);
}
