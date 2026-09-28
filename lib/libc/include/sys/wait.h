#pragma once

#include <sys/types.h>

#define WNOHANG 1

#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
