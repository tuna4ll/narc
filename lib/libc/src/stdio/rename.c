#include "../internal/narc.h"
#include <stdio.h>
#include <string.h>

int rename(const char *old_path, const char *new_path) {
    return (int)__libc_result(narc_rename(old_path, strlen(old_path),
                                          new_path, strlen(new_path)));
}
