#pragma once
#include <stdint.h>

#define TIME_HZ 100
#define NSEC_PER_SEC 1000000000ULL

struct ktimer {
    struct ktimer *next;
    uint64_t deadline;
    void (*expire)(struct ktimer *timer);
    void *data;
    int armed;
};

uint64_t arch_clock_ns(void);
int64_t arch_rtc_seconds(void);

void time_init(void);
void time_tick(void);
uint64_t time_monotonic_ns(void);
int64_t time_realtime_ns(void);
void time_set_realtime_ns(int64_t value);
void ktimer_arm(struct ktimer *timer, uint64_t deadline);
void ktimer_cancel(struct ktimer *timer);
int time_sleep_until(uint64_t deadline);
