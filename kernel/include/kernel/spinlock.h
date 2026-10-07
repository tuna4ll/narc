#pragma once
#include <kernel/arch.h>
#include <stdint.h>

struct spinlock {
    uint32_t locked;
};

#define SPINLOCK_INIT { 0 }

static inline void spin_init(struct spinlock *lock) {
    lock->locked = 0;
}

static inline void spin_lock(struct spinlock *lock) {
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) arch_cpu_relax();
}

static inline int spin_trylock(struct spinlock *lock) {
    return !__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE);
}

static inline void spin_unlock(struct spinlock *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

static inline int spin_is_locked(struct spinlock *lock) {
    return __atomic_load_n(&lock->locked, __ATOMIC_RELAXED) != 0;
}

static inline unsigned long spin_lock_irqsave(struct spinlock *lock) {
    unsigned long flags = arch_irq_save();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irqrestore(struct spinlock *lock, unsigned long flags) {
    spin_unlock(lock);
    arch_irq_restore(flags);
}
