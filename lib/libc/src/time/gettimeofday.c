#include <sys/time.h>
#include <time.h>

int gettimeofday(struct timeval *value, void *zone) {
    (void)zone;
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return -1;
    value->tv_sec = now.tv_sec;
    value->tv_usec = now.tv_nsec / 1000;
    return 0;
}
