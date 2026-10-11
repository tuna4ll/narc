#include "../internal/narc.h"
#include <string.h>
#include <unistd.h>

int chdir(const char *path) {
    return (int)__libc_result(narc_chdir(path, strlen(path)));
}
