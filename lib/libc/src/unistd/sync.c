#include "../internal/narc.h"
#include <unistd.h>

void sync(void) {
    narc_sync(-1);
}

int fsync(int fd) {
    return __libc_result(narc_sync(fd)) < 0 ? -1 : 0;
}

int fdatasync(int fd) {
    return fsync(fd);
}
