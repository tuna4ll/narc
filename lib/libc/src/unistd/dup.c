#include "../internal/narc.h"
#include <unistd.h>

int dup(int fd) {
    return (int)__libc_result(narc_dup(fd, 0));
}

int dup2(int fd, int target) {
    return (int)__libc_result(narc_dup2(fd, target));
}
