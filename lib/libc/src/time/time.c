#include <time.h>

time_t time(time_t *result) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return (time_t)-1;
    if (result) *result = now.tv_sec;
    return now.tv_sec;
}
