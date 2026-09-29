#include "../internal/narc.h"
#include <string.h>
#include <sys/stat.h>

int mkdir(const char *path, mode_t mode) {
    return (int)__libc_result(narc_mkdir(path, strlen(path), mode));
}
