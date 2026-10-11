#pragma once
#include <kernel/arch.h>
#include <narc/abi.h>
#include <stdint.h>

#define SIG_BIT(signal) (UINT64_C(1) << (signal))
#define SIG_UNBLOCKABLE (SIG_BIT(NARC_SIGKILL) | SIG_BIT(NARC_SIGSTOP))
#define SIG_VALID(signal) ((signal) > 0 && (signal) < NARC_NSIG)

struct task;

struct signal_state {
    uint64_t pending;
    uint64_t blocked;
    uint64_t saved_mask;
    int restore_mask;
    narc_sigaction_t actions[NARC_NSIG];
    narc_siginfo_t info[NARC_NSIG];
};

void signal_fork(struct task *child, struct task *parent);
void signal_exec(struct task *task);
int signal_send(struct task *task, int signal, const narc_siginfo_t *info);
void signal_force(int signal, uint64_t address);
int signal_pending(void);
void signal_deliver(struct task_frame *frame);
void user_return(struct task_frame *frame);

long signal_action(int signal, uint64_t action, uint64_t old_action);
long signal_mask(int how, uint64_t set);
long signal_suspend(uint64_t mask);
int signal_return(struct task_frame *frame);

int arch_signal_enter(struct task_frame *frame, uint64_t handler, uint64_t sp,
                      uint64_t restorer, uint64_t arg0, uint64_t arg1, uint64_t arg2);
uint64_t arch_frame_sp(const struct task_frame *frame);
int arch_frame_sanitize(struct task_frame *frame, void *state);
void arch_syscall_restart(struct task_frame *frame, const uint64_t saved[4]);
