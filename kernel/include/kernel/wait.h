#pragma once
#include <kernel/sched.h>
#include <kernel/spinlock.h>

struct task;

struct waiter {
    struct task *task;
    struct waiter *next;
    int queued;
};

struct wait_queue {
    struct spinlock lock;
    struct waiter *head, *tail;
};

#define WAIT_QUEUE_INIT { SPINLOCK_INIT, 0, 0 }

void wait_queue_init(struct wait_queue *queue);
void wait_prepare(struct wait_queue *queue, struct waiter *waiter);
void wait_finish(struct wait_queue *queue, struct waiter *waiter);
void wake_up_one(struct wait_queue *queue);
void wake_up_all(struct wait_queue *queue);

#define wait_event(queue, condition) do { \
    struct waiter wait_event_waiter = { 0 }; \
    for (;;) { \
        wait_prepare((queue), &wait_event_waiter); \
        if (condition) break; \
        schedule(); \
    } \
    wait_finish((queue), &wait_event_waiter); \
} while (0)

#define wait_event_locked(queue, condition, lock) do { \
    struct waiter wait_event_waiter = { 0 }; \
    while (!(condition)) { \
        wait_prepare((queue), &wait_event_waiter); \
        spin_unlock(lock); \
        schedule(); \
        spin_lock(lock); \
    } \
    wait_finish((queue), &wait_event_waiter); \
} while (0)
