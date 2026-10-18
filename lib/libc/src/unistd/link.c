#include "../internal/narc.h"
#include <unistd.h>

int symlink(const char *target, const char *path) {
    return (int)__libc_result(narc_symlink(target, path));
}

int link(const char *old_path, const char *new_path) {
    return (int)__libc_result(narc_link(old_path, new_path));
}

ssize_t readlink(const char *restrict path, char *restrict buffer, size_t size) {
    return (ssize_t)__libc_result(narc_readlink(path, buffer, size));
}
