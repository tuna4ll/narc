#pragma once
#include <kernel/spinlock.h>
#include <kernel/wait.h>

struct task;

struct mutex {
    struct spinlock lock;
    int locked;
    struct task *owner;
    struct wait_queue waiters;
};

#define MUTEX_INIT { SPINLOCK_INIT, 0, 0, WAIT_QUEUE_INIT }

void mutex_init(struct mutex *mutex);
void mutex_lock(struct mutex *mutex);
int mutex_trylock(struct mutex *mutex);
void mutex_unlock(struct mutex *mutex);
int mutex_held(struct mutex *mutex);
