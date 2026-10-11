#include "../internal/narc.h"
#include <errno.h>
#include <time.h>

int clock_gettime(clockid_t clock, struct timespec *value) {
    long nanoseconds = __libc_result(narc_clock_get((uint32_t)clock));
    if (nanoseconds < 0 && errno) return -1;
    value->tv_sec = nanoseconds / 1000000000L;
    value->tv_nsec = nanoseconds % 1000000000L;
    if (value->tv_nsec < 0) {
        value->tv_sec--;
        value->tv_nsec += 1000000000L;
    }
    return 0;
}
