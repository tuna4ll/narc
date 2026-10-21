#pragma once

#include <stdbool.h>
#include <time.h>

time_t utc_mktime(struct tm *tm);
long utc_offset(time_t when);
bool local_time(time_t when, struct tm *result);
bool parse_datetime(struct timespec *result, const char *string, const struct timespec *now);
char *format_time(const char *format, const struct tm *tm, time_t seconds, long nanoseconds,
                  bool utc);
