#include "../internal/narc.h"
#include <string.h>
#include <unistd.h>

int rmdir(const char *path) {
    return (int)__libc_result(narc_rmdir(path, strlen(path)));
}
