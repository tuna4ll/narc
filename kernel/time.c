#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/time.h>

static struct spinlock timer_lock = SPINLOCK_INIT;
static struct ktimer *timers;
static int64_t realtime_offset;

void time_init(void) {
    realtime_offset = arch_rtc_seconds() * (int64_t)NSEC_PER_SEC - (int64_t)arch_clock_ns();
}

uint64_t time_monotonic_ns(void) {
    return arch_clock_ns();
}

int64_t time_realtime_ns(void) {
    return realtime_offset + (int64_t)arch_clock_ns();
}

void time_set_realtime_ns(int64_t value) {
    realtime_offset = value - (int64_t)arch_clock_ns();
}

static void unlink_timer(struct ktimer *timer) {
    struct ktimer **link = &timers;
    while (*link && *link != timer) link = &(*link)->next;
    if (*link) *link = timer->next;
    timer->next = 0;
    timer->armed = 0;
}

void ktimer_arm(struct ktimer *timer, uint64_t deadline) {
    unsigned long flags = spin_lock_irqsave(&timer_lock);
    if (timer->armed) unlink_timer(timer);
    timer->deadline = deadline;
    struct ktimer **link = &timers;
    while (*link && (*link)->deadline <= deadline) link = &(*link)->next;
    timer->next = *link;
    *link = timer;
    timer->armed = 1;
    spin_unlock_irqrestore(&timer_lock, flags);
}

void ktimer_cancel(struct ktimer *timer) {
    unsigned long flags = spin_lock_irqsave(&timer_lock);
    if (timer->armed) unlink_timer(timer);
    spin_unlock_irqrestore(&timer_lock, flags);
}

void time_tick(void) {
    uint64_t now = arch_clock_ns();
    for (;;) {
        unsigned long flags = spin_lock_irqsave(&timer_lock);
        struct ktimer *timer = timers;
        if (timer && timer->deadline <= now) unlink_timer(timer);
        else timer = 0;
        spin_unlock_irqrestore(&timer_lock, flags);
        if (!timer) break;
        timer->expire(timer);
    }
    sched_tick();
}

static void wake_sleeper(struct ktimer *timer) {
    sched_wake(timer->data);
}

int time_sleep_until(uint64_t deadline) {
    struct ktimer timer = { .expire = wake_sleeper, .data = task_current() };
    ktimer_arm(&timer, deadline);
    for (;;) {
        sched_sleep_prepare();
        if (arch_clock_ns() >= deadline) break;
        schedule();
    }
    sched_sleep_cancel();
    ktimer_cancel(&timer);
    return 0;
}
