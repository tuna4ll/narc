#include "../internal/narc.h"
#include <unistd.h>

pid_t fork(void) {
    return (pid_t)__libc_result(narc_fork());
}
