#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>

static struct spinlock rq_lock = SPINLOCK_INIT;
static struct task *rq_head, *rq_tail;
static struct task idle_task;
static struct task *current_task = &idle_task;
static struct task *switched_from;
static int need_resched;

struct task *task_current(void) {
    return current_task;
}

struct address_space *sched_kernel_space(void) {
    return &idle_task.space;
}

void sched_init(uint64_t boot_stack_top) {
    vmm_space_boot(&idle_task.space);
    idle_task.state = TASK_RUNNABLE;
    idle_task.on_cpu = 1;
    idle_task.kstack_top = boot_stack_top;
    arch_task_state_init(idle_task.arch_state);
}

static void rq_push(struct task *task) {
    task->run_next = 0;
    task->on_rq = 1;
    if (rq_tail) rq_tail->run_next = task;
    else rq_head = task;
    rq_tail = task;
}

static struct task *rq_pop(void) {
    struct task *task = rq_head;
    if (!task) return 0;
    rq_head = task->run_next;
    if (!rq_head) rq_tail = 0;
    task->run_next = 0;
    task->on_rq = 0;
    return task;
}

static void make_runnable(struct task *task) {
    task->state = TASK_RUNNABLE;
    if (!task->on_rq && !task->on_cpu) rq_push(task);
}

static void finish_switch(void) {
    if (switched_from) __atomic_store_n(&switched_from->on_cpu, 0, __ATOMIC_RELEASE);
    switched_from = 0;
}

static void switch_to(struct task *prev, struct task *next) {
    arch_task_state_save(prev->arch_state);
    current_task = next;
    next->on_cpu = 1;
    vmm_space_activate(&next->space);
    arch_set_tls(next->fs_base);
    arch_task_state_restore(next->arch_state);
    arch_set_kernel_stack(next->kstack_top);
    switched_from = prev;
    arch_context_switch(&prev->context, next->context);
    finish_switch();
}

static struct task *pick_next(void) {
    struct task *next = rq_pop();
    return next ? next : &idle_task;
}

void sched_start(struct task *task) {
    unsigned long flags = spin_lock_irqsave(&rq_lock);
    make_runnable(task);
    spin_unlock_irqrestore(&rq_lock, flags);
}

void sched_wake(struct task *task) {
    unsigned long flags = spin_lock_irqsave(&rq_lock);
    if (task->state == TASK_SLEEPING) make_runnable(task);
    spin_unlock_irqrestore(&rq_lock, flags);
}

void sched_sleep_prepare(void) {
    unsigned long flags = spin_lock_irqsave(&rq_lock);
    current_task->state = TASK_SLEEPING;
    spin_unlock_irqrestore(&rq_lock, flags);
}

void sched_sleep_cancel(void) {
    unsigned long flags = spin_lock_irqsave(&rq_lock);
    current_task->state = TASK_RUNNABLE;
    spin_unlock_irqrestore(&rq_lock, flags);
}

void schedule(void) {
    unsigned long flags = spin_lock_irqsave(&rq_lock);
    struct task *prev = current_task;
    if (prev->state == TASK_RUNNABLE && prev != &idle_task) rq_push(prev);
    struct task *next = pick_next();
    need_resched = 0;
    if (next != prev) switch_to(prev, next);
    spin_unlock_irqrestore(&rq_lock, flags);
}

void sched_tick(void) {
    need_resched = 1;
}

void sched_user_return(void) {
    if (need_resched) schedule();
}

void sched_tail(void) {
    finish_switch();
    spin_unlock(&rq_lock);
}

void sched_exit(void) {
    arch_irq_disable();
    spin_lock(&rq_lock);
    current_task->state = TASK_ZOMBIE;
    switch_to(current_task, pick_next());
    for (;;) arch_halt();
}

void sched_idle(void) {
    for (;;) {
        arch_irq_disable();
        spin_lock(&rq_lock);
        int runnable = rq_head != 0;
        spin_unlock(&rq_lock);
        if (runnable) schedule();
        else arch_idle();
    }
}
