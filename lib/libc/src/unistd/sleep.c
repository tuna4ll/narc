#include <time.h>
#include <unistd.h>

unsigned int sleep(unsigned int seconds) {
    struct timespec request = { seconds, 0 }, remaining = { 0, 0 };
    if (nanosleep(&request, &remaining) == 0) return 0;
    return (unsigned int)remaining.tv_sec + (remaining.tv_nsec > 0);
}

int usleep(useconds_t microseconds) {
    struct timespec request = { microseconds / 1000000, (long)(microseconds % 1000000) * 1000 };
    return nanosleep(&request, 0);
}
