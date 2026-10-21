#pragma once

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <narcutils/error.h>
#include <narcutils/options.h>
#include <narcutils/xalloc.h>

#define NARCUTILS_VERSION "0.1.0"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define STREQ(left, right) (strcmp(left, right) == 0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define EXIT_TROUBLE 2
#define EXIT_TIMEDOUT 124
#define EXIT_CANCELED 125
#define EXIT_CANNOT_INVOKE 126
#define EXIT_ENOENT 127

static inline bool dot_or_dotdot(const char *name) {
    return name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]));
}
