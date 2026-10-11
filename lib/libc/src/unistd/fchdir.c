#include "../internal/narc.h"
#include <unistd.h>

int fchdir(int fd) {
    return (int)__libc_result(narc_fchdir(fd));
}
