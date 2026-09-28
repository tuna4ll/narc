#include "../internal/narc.h"
#include <unistd.h>

int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)__libc_result(narc_exec(path, argv, envp));
}
