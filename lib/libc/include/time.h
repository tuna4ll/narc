#pragma once

#include <stddef.h>
#include <sys/types.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

int clock_gettime(clockid_t clock, struct timespec *value);
int nanosleep(const struct timespec *request, struct timespec *remaining);
time_t time(time_t *result);
struct tm *gmtime_r(const time_t *value, struct tm *result);
struct tm *gmtime(const time_t *value);
struct tm *localtime_r(const time_t *value, struct tm *result);
struct tm *localtime(const time_t *value);
time_t mktime(struct tm *value);
size_t strftime(char *buffer, size_t size, const char *format, const struct tm *value);
