#include "../internal/narc.h"
#include <errno.h>
#include <time.h>

int nanosleep(const struct timespec *request, struct timespec *remaining) {
    if (request->tv_sec < 0 || request->tv_nsec < 0 || request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    uint64_t total = (uint64_t)request->tv_sec * 1000000000ULL + (uint64_t)request->tv_nsec;
    narc_result_t result = narc_sleep(total);
    if (result.status == NARC_OK) return 0;
    if (remaining) {
        remaining->tv_sec = result.value / 1000000000L;
        remaining->tv_nsec = result.value % 1000000000L;
    }
    return (int)__libc_result(result);
}
