#include "../internal/narc.h"
#include <string.h>
#include <unistd.h>

int unlink(const char *path) {
    return (int)__libc_result(narc_unlink(path, strlen(path)));
}
