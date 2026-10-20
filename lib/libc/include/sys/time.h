#pragma once

#include <sys/types.h>

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

int gettimeofday(struct timeval *value, void *zone);
int utimes(const char *path, const struct timeval times[2]);
