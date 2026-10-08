#include <kernel/arch.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/sched.h>

extern char riscv_trap_entry[];

static uint64_t timer_ticks = 100000;

static void timer_reset(void) {
    uint64_t now;
    __asm__ volatile ("rdtime %0" : "=r"(now));
    register uint64_t a0 __asm__("a0") = now + timer_ticks;
    register uint64_t a6 __asm__("a6") = 0;
    register uint64_t a7 __asm__("a7") = 0x54494d45;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a6), "r"(a7) : "memory");
}

void arch_init(uint64_t kernel_stack) {
    (void)kernel_stack;
    __asm__ volatile ("csrw stvec, %0; csrw sscratch, zero"
                      : : "r"(riscv_trap_entry) : "memory");
    uint64_t sie;
    __asm__ volatile ("csrr %0, sie" : "=r"(sie));
    sie |= 1ULL << 5;
    __asm__ volatile ("csrw sie, %0; csrs sstatus, %1"
                      : : "r"(sie), "r"(3ULL << 13));
    arch_irq_disable();
    timer_reset();
}

void arch_halt(void) {
    for (;;) __asm__ volatile ("csrci sstatus, 2; wfi");
}

void arch_set_tls(uint64_t value) {
    __asm__ volatile ("mv tp, %0" : : "r"(value));
}

void arch_task_frame_init(struct task_frame *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->status = (1ULL << 5) | (3ULL << 13);
}

void arch_task_set_entry(struct task_frame *frame, uint64_t pc, uint64_t sp) {
    frame->pc = pc;
    frame->x[2] = sp;
}

void arch_task_state_init(void *state) {
    memset(state, 0, ARCH_STATE_SIZE);
}

uint64_t arch_syscall_number(const struct task_frame *frame) {
    return frame->x[17];
}

uint64_t arch_syscall_arg(const struct task_frame *frame, unsigned index) {
    return index < 6 ? frame->x[10 + index] : 0;
}

void arch_syscall_return(struct task_frame *frame, uint64_t value) {
    frame->x[10] = value;
}

void arch_syscall_return2(struct task_frame *frame, uint64_t value, uint64_t status) {
    frame->x[10] = value;
    frame->x[11] = status;
}

int arch_frame_from_user(const struct task_frame *frame) {
    return !(frame->status & (1ULL << 8));
}

uint64_t arch_context_init(uint64_t stack_top, void (*entry)(void)) {
    uint64_t *sp = (uint64_t *)(uintptr_t)((stack_top & ~0xfULL) - 112);
    memset(sp, 0, 112);
    sp[0] = (uint64_t)(uintptr_t)entry;
    return (uint64_t)(uintptr_t)sp;
}

void arch_set_kernel_stack(uint64_t top) {
    (void)top;
}

void riscv_trap(struct task_frame *frame) {
    uint64_t cause;
    __asm__ volatile ("csrr %0, scause" : "=r"(cause));
    if (cause == ((1ULL << 63) | 5)) {
        timer_reset();
        sched_tick();
        if (arch_frame_from_user(frame)) sched_user_return();
        return;
    }
    if (cause == 8) {
        frame->pc += 4;
        syscall_dispatch(frame);
        return;
    }
    arch_halt();
}
