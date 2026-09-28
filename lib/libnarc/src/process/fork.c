#include "../internal/syscall.h"

narc_result_t narc_fork(void) {
    return __narc_call3(NARC_SYS_FORK, 0, 0, 0);
}
