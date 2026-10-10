#include <kernel/mutex.h>
#include <kernel/sched.h>

void mutex_init(struct mutex *mutex) {
    spin_init(&mutex->lock);
    mutex->locked = 0;
    mutex->owner = 0;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(struct mutex *mutex) {
    spin_lock(&mutex->lock);
    wait_event_locked(&mutex->waiters, !mutex->locked, &mutex->lock);
    mutex->locked = 1;
    mutex->owner = task_current();
    spin_unlock(&mutex->lock);
}

int mutex_trylock(struct mutex *mutex) {
    spin_lock(&mutex->lock);
    int acquired = !mutex->locked;
    if (acquired) {
        mutex->locked = 1;
        mutex->owner = task_current();
    }
    spin_unlock(&mutex->lock);
    return acquired;
}

void mutex_unlock(struct mutex *mutex) {
    spin_lock(&mutex->lock);
    mutex->locked = 0;
    mutex->owner = 0;
    spin_unlock(&mutex->lock);
    wake_up_one(&mutex->waiters);
}

int mutex_held(struct mutex *mutex) {
    return mutex->locked && mutex->owner == task_current();
}
