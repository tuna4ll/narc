#pragma once
#include <kernel/mm.h>
#include <stdint.h>

struct task;

void sched_init(uint64_t boot_stack_top);
struct task *task_current(void);
struct address_space *sched_kernel_space(void);
void sched_start(struct task *task);
void sched_wake(struct task *task);
void sched_sleep_prepare(void);
void sched_sleep_cancel(void);
void schedule(void);
void sched_tick(void);
void sched_user_return(void);
void sched_tail(void);
void sched_exit(void) __attribute__((noreturn));
void sched_idle(void) __attribute__((noreturn));
