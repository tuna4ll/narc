#include <kernel/heap.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/uaccess.h>

enum signal_default {
    SIGNAL_TERM,
    SIGNAL_CORE,
    SIGNAL_IGNORE,
    SIGNAL_STOP,
    SIGNAL_CONT,
};

static const uint8_t defaults[NARC_NSIG] = {
    [NARC_SIGHUP] = SIGNAL_TERM,    [NARC_SIGINT] = SIGNAL_TERM,
    [NARC_SIGQUIT] = SIGNAL_CORE,   [NARC_SIGILL] = SIGNAL_CORE,
    [NARC_SIGTRAP] = SIGNAL_CORE,   [NARC_SIGABRT] = SIGNAL_CORE,
    [NARC_SIGBUS] = SIGNAL_CORE,    [NARC_SIGFPE] = SIGNAL_CORE,
    [NARC_SIGKILL] = SIGNAL_TERM,   [NARC_SIGUSR1] = SIGNAL_TERM,
    [NARC_SIGSEGV] = SIGNAL_CORE,   [NARC_SIGUSR2] = SIGNAL_TERM,
    [NARC_SIGPIPE] = SIGNAL_TERM,   [NARC_SIGALRM] = SIGNAL_TERM,
    [NARC_SIGTERM] = SIGNAL_TERM,   [NARC_SIGSTKFLT] = SIGNAL_TERM,
    [NARC_SIGCHLD] = SIGNAL_IGNORE, [NARC_SIGCONT] = SIGNAL_CONT,
    [NARC_SIGSTOP] = SIGNAL_STOP,   [NARC_SIGTSTP] = SIGNAL_STOP,
    [NARC_SIGTTIN] = SIGNAL_STOP,   [NARC_SIGTTOU] = SIGNAL_STOP,
    [NARC_SIGURG] = SIGNAL_IGNORE,  [NARC_SIGXCPU] = SIGNAL_CORE,
    [NARC_SIGXFSZ] = SIGNAL_CORE,   [NARC_SIGVTALRM] = SIGNAL_TERM,
    [NARC_SIGPROF] = SIGNAL_TERM,   [NARC_SIGWINCH] = SIGNAL_IGNORE,
    [NARC_SIGIO] = SIGNAL_TERM,     [NARC_SIGPWR] = SIGNAL_TERM,
    [NARC_SIGSYS] = SIGNAL_CORE,
};

struct signal_frame {
    narc_siginfo_t info;
    uint64_t mask;
    uint64_t reserved;
    struct task_frame regs;
    uint8_t state[ARCH_STATE_SIZE] __attribute__((aligned(16)));
};

static struct spinlock signal_lock = SPINLOCK_INIT;

static int discarded(struct signal_state *state, int signal) {
    if (state->blocked & SIG_BIT(signal)) return 0;
    uint64_t handler = state->actions[signal].handler;
    if (handler == NARC_SIG_IGN) return 1;
    return handler == NARC_SIG_DFL && defaults[signal] != SIGNAL_TERM &&
           defaults[signal] != SIGNAL_CORE;
}

void signal_fork(struct task *child, struct task *parent) {
    unsigned long flags = spin_lock_irqsave(&signal_lock);
    child->signals = parent->signals;
    child->signals.pending = 0;
    child->signals.restore_mask = 0;
    spin_unlock_irqrestore(&signal_lock, flags);
}

void signal_exec(struct task *task) {
    unsigned long flags = spin_lock_irqsave(&signal_lock);
    for (int signal = 1; signal < NARC_NSIG; signal++) {
        narc_sigaction_t *action = &task->signals.actions[signal];
        uint64_t handler = action->handler == NARC_SIG_IGN ? NARC_SIG_IGN : NARC_SIG_DFL;
        *action = (narc_sigaction_t) { .handler = handler };
    }
    spin_unlock_irqrestore(&signal_lock, flags);
}

int signal_send(struct task *task, int signal, const narc_siginfo_t *info) {
    if (!signal) return 0;
    if (!SIG_VALID(signal)) return -NARC_INVALID_ARGUMENT;
    struct signal_state *state = &task->signals;
    unsigned long flags = spin_lock_irqsave(&signal_lock);
    int wake = 0;
    int protected = task->pid == 1 && state->actions[signal].handler == NARC_SIG_DFL;
    if (!task->exited && !protected && !discarded(state, signal)) {
        if (!(state->pending & SIG_BIT(signal))) {
            state->info[signal] = info ? *info : (narc_siginfo_t) { .code = NARC_SI_KERNEL };
            state->info[signal].signo = signal;
        }
        state->pending |= SIG_BIT(signal);
        wake = !(state->blocked & SIG_BIT(signal));
    }
    spin_unlock_irqrestore(&signal_lock, flags);
    if (wake) sched_wake_interruptible(task);
    return 0;
}

void signal_force(int signal, uint64_t address) {
    struct task *self = task_current();
    unsigned long flags = spin_lock_irqsave(&signal_lock);
    struct signal_state *state = &self->signals;
    if ((state->blocked & SIG_BIT(signal)) || state->actions[signal].handler == NARC_SIG_IGN) {
        state->actions[signal] = (narc_sigaction_t) { .handler = NARC_SIG_DFL };
        state->blocked &= ~SIG_BIT(signal);
    }
    spin_unlock_irqrestore(&signal_lock, flags);
    narc_siginfo_t info = { .code = NARC_SI_KERNEL, .address = address };
    if (self->pid == 1 && self->signals.actions[signal].handler == NARC_SIG_DFL)
        task_exit_signal(signal);
    signal_send(self, signal, &info);
}

int signal_pending(void) {
    struct signal_state *state = &task_current()->signals;
    return (__atomic_load_n(&state->pending, __ATOMIC_ACQUIRE) & ~state->blocked) != 0;
}

static int setup_frame(struct task *self, struct task_frame *frame, const narc_siginfo_t *info,
                       const narc_sigaction_t *action, uint64_t mask) {
    struct signal_frame *kframe = kzalloc(sizeof(*kframe));
    if (!kframe) return -1;
    kframe->info = *info;
    kframe->mask = mask;
    kframe->regs = *frame;
    arch_task_state_save(self->arch_state);
    memcpy(kframe->state, self->arch_state, ARCH_STATE_SIZE);
    uint64_t sp = (arch_frame_sp(frame) - ARCH_RED_ZONE - sizeof(*kframe)) & ~UINT64_C(15);
    struct address_space *space = vmm_space_current();
    int status = -1;
    if (vmm_user_range_ok(space, sp, sizeof(*kframe), 1) &&
        uaccess_write(space, sp, kframe, sizeof(*kframe)) == 0)
        status = arch_signal_enter(frame, action->handler, sp, action->restorer,
                                   (uint64_t)info->signo,
                                   sp + __builtin_offsetof(struct signal_frame, info), sp);
    kfree(kframe);
    return status;
}

static void finish_syscall(struct task *self, struct task_frame *frame, int restart) {
    if (self->syscall_restart && restart) arch_syscall_restart(frame, self->syscall_saved);
    self->syscall_restart = 0;
}

void signal_deliver(struct task_frame *frame) {
    struct task *self = task_current();
    struct signal_state *state = &self->signals;
    if (!(state->pending & ~state->blocked) && !self->syscall_restart && !state->restore_mask)
        return;
    arch_irq_enable();
    for (;;) {
        unsigned long flags = spin_lock_irqsave(&signal_lock);
        uint64_t ready = state->pending & ~state->blocked;
        if (!ready) {
            spin_unlock_irqrestore(&signal_lock, flags);
            break;
        }
        int signal = 1;
        while (!(ready & SIG_BIT(signal))) signal++;
        state->pending &= ~SIG_BIT(signal);
        narc_siginfo_t info = state->info[signal];
        narc_sigaction_t action = state->actions[signal];
        spin_unlock_irqrestore(&signal_lock, flags);

        if (action.handler == NARC_SIG_IGN) continue;
        if (action.handler == NARC_SIG_DFL) {
            if (defaults[signal] == SIGNAL_TERM) task_exit_signal(signal);
            if (defaults[signal] == SIGNAL_CORE) task_exit_signal(signal | 0x80);
            continue;
        }

        finish_syscall(self, frame, (action.flags & NARC_SA_RESTART) != 0);
        flags = spin_lock_irqsave(&signal_lock);
        uint64_t mask = state->restore_mask ? state->saved_mask : state->blocked;
        state->restore_mask = 0;
        spin_unlock_irqrestore(&signal_lock, flags);
        if (setup_frame(self, frame, &info, &action, mask) != 0) task_exit_signal(NARC_SIGSEGV);
        flags = spin_lock_irqsave(&signal_lock);
        state->blocked |= action.mask | (action.flags & NARC_SA_NODEFER ? 0 : SIG_BIT(signal));
        state->blocked &= ~SIG_UNBLOCKABLE;
        if (action.flags & NARC_SA_RESETHAND)
            state->actions[signal] = (narc_sigaction_t) { .handler = NARC_SIG_DFL };
        spin_unlock_irqrestore(&signal_lock, flags);
        arch_irq_disable();
        return;
    }
    finish_syscall(self, frame, 1);
    if (state->restore_mask) {
        state->blocked = state->saved_mask;
        state->restore_mask = 0;
    }
    arch_irq_disable();
}

void user_return(struct task_frame *frame) {
    sched_user_return();
    signal_deliver(frame);
}

long signal_action(int signal, uint64_t new_address, uint64_t old_address) {
    if (!SIG_VALID(signal)) return -NARC_INVALID_ARGUMENT;
    struct task *self = task_current();
    struct address_space *space = vmm_space_current();
    narc_sigaction_t action;
    if (new_address) {
        if (signal == NARC_SIGKILL || signal == NARC_SIGSTOP) return -NARC_INVALID_ARGUMENT;
        if (!vmm_user_range_ok(space, new_address, sizeof(action), 0) ||
            uaccess_read(space, &action, new_address, sizeof(action)) != 0)
            return -NARC_BAD_ADDRESS;
        action.mask &= ~SIG_UNBLOCKABLE;
    }
    unsigned long flags = spin_lock_irqsave(&signal_lock);
    narc_sigaction_t old = self->signals.actions[signal];
    if (new_address) {
        self->signals.actions[signal] = action;
        if (discarded(&self->signals, signal)) self->signals.pending &= ~SIG_BIT(signal);
    }
    spin_unlock_irqrestore(&signal_lock, flags);
    if (old_address && (!vmm_user_range_ok(space, old_address, sizeof(old), 1) ||
                        uaccess_write(space, old_address, &old, sizeof(old)) != 0))
        return -NARC_BAD_ADDRESS;
    return 0;
}

long signal_mask(int how, uint64_t set) {
    struct signal_state *state = &task_current()->signals;
    unsigned long flags = spin_lock_irqsave(&signal_lock);
    uint64_t old = state->blocked;
    long status = 0;
    switch (how) {
    case NARC_SIG_BLOCK:   state->blocked |= set; break;
    case NARC_SIG_UNBLOCK: state->blocked &= ~set; break;
    case NARC_SIG_SETMASK: state->blocked = set; break;
    default:               status = -NARC_INVALID_ARGUMENT; break;
    }
    state->blocked &= ~(SIG_UNBLOCKABLE | 1);
    spin_unlock_irqrestore(&signal_lock, flags);
    return status ? status : (long)old;
}

long signal_suspend(uint64_t mask) {
    struct signal_state *state = &task_current()->signals;
    unsigned long flags = spin_lock_irqsave(&signal_lock);
    state->saved_mask = state->blocked;
    state->restore_mask = 1;
    state->blocked = mask & ~(SIG_UNBLOCKABLE | 1);
    spin_unlock_irqrestore(&signal_lock, flags);
    for (;;) {
        sched_sleep_prepare(1);
        if (signal_pending()) break;
        schedule();
    }
    sched_sleep_cancel();
    return -NARC_INTERRUPTED;
}

int signal_return(struct task_frame *frame) {
    struct task *self = task_current();
    struct address_space *space = vmm_space_current();
    uint64_t sp = arch_frame_sp(frame);
    struct signal_frame *kframe = kmalloc(sizeof(*kframe));
    int status = -1;
    if (kframe && vmm_user_range_ok(space, sp, sizeof(*kframe), 0) &&
        uaccess_read(space, kframe, sp, sizeof(*kframe)) == 0 &&
        arch_frame_sanitize(&kframe->regs, kframe->state) == 0) {
        *frame = kframe->regs;
        memcpy(self->arch_state, kframe->state, ARCH_STATE_SIZE);
        arch_task_state_restore(self->arch_state);
        unsigned long flags = spin_lock_irqsave(&signal_lock);
        self->signals.blocked = kframe->mask & ~(SIG_UNBLOCKABLE | 1);
        spin_unlock_irqrestore(&signal_lock, flags);
        status = 0;
    }
    kfree(kframe);
    if (status) signal_force(NARC_SIGSEGV, sp);
    return status;
}
