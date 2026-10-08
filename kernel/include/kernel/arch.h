#pragma once
#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)
struct task_frame {
    uint64_t r15, r14, r13, r12, rbp, rbx;
    uint64_t rax, rdi, rsi, rdx, r10, r8, r9;
    uint64_t rip, cs, rflags, rsp, ss;
};
#define ARCH_STATE_SIZE 512
#define ARCH_ELF_MACHINE 62
#elif defined(__aarch64__)
struct task_frame {
    uint64_t x[31];
    uint64_t sp, pc, pstate;
};
#define ARCH_STATE_SIZE 536
#define ARCH_ELF_MACHINE 183
#elif defined(__riscv)
struct task_frame {
    uint64_t x[32];
    uint64_t pc, status;
};
#define ARCH_STATE_SIZE 264
#define ARCH_ELF_MACHINE 243
#else
#error Unsupported architecture
#endif

void arch_init(uint64_t kernel_stack);
void arch_halt(void) __attribute__((noreturn));
void arch_set_tls(uint64_t value);
void arch_task_frame_init(struct task_frame *frame);
void arch_task_set_entry(struct task_frame *frame, uint64_t pc, uint64_t sp);
void arch_task_state_init(void *state);
void arch_task_state_save(void *state);
void arch_task_state_restore(const void *state);
uint64_t arch_syscall_number(const struct task_frame *frame);
uint64_t arch_syscall_arg(const struct task_frame *frame, unsigned index);
void arch_syscall_return(struct task_frame *frame, uint64_t value);
void arch_syscall_return2(struct task_frame *frame, uint64_t value, uint64_t status);
int arch_frame_from_user(const struct task_frame *frame);
uint64_t arch_context_init(uint64_t stack_top, void (*entry)(void));
void arch_context_switch(uint64_t *prev, uint64_t next);
void arch_set_kernel_stack(uint64_t top);
void arch_enter_user(struct task_frame *frame) __attribute__((noreturn));

#if defined(__x86_64__)
static inline unsigned long arch_irq_save(void) {
    unsigned long flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags & 0x200;
}

static inline void arch_irq_restore(unsigned long flags) {
    if (flags) __asm__ volatile ("sti" ::: "memory");
}

static inline void arch_irq_enable(void) { __asm__ volatile ("sti" ::: "memory"); }
static inline void arch_irq_disable(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void arch_cpu_relax(void) { __asm__ volatile ("pause" ::: "memory"); }
static inline void arch_idle(void) { __asm__ volatile ("sti; hlt; cli" ::: "memory"); }
#elif defined(__aarch64__)
static inline unsigned long arch_irq_save(void) {
    unsigned long flags;
    __asm__ volatile ("mrs %0, daif; msr daifset, #2" : "=r"(flags) : : "memory");
    return flags;
}

static inline void arch_irq_restore(unsigned long flags) {
    __asm__ volatile ("msr daif, %0" : : "r"(flags) : "memory");
}

static inline void arch_irq_enable(void) { __asm__ volatile ("msr daifclr, #2" ::: "memory"); }
static inline void arch_irq_disable(void) { __asm__ volatile ("msr daifset, #2" ::: "memory"); }
static inline void arch_cpu_relax(void) { __asm__ volatile ("yield" ::: "memory"); }
static inline void arch_idle(void) { __asm__ volatile ("wfi; msr daifclr, #2; isb; msr daifset, #2" ::: "memory"); }
#elif defined(__riscv)
static inline unsigned long arch_irq_save(void) {
    unsigned long flags;
    __asm__ volatile ("csrrci %0, sstatus, 2" : "=r"(flags) : : "memory");
    return flags & 2;
}

static inline void arch_irq_restore(unsigned long flags) {
    if (flags) __asm__ volatile ("csrsi sstatus, 2" ::: "memory");
}

static inline void arch_irq_enable(void) { __asm__ volatile ("csrsi sstatus, 2" ::: "memory"); }
static inline void arch_irq_disable(void) { __asm__ volatile ("csrci sstatus, 2" ::: "memory"); }
static inline void arch_cpu_relax(void) { __asm__ volatile ("nop" ::: "memory"); }
static inline void arch_idle(void) { __asm__ volatile ("wfi; csrsi sstatus, 2; csrci sstatus, 2" ::: "memory"); }
#endif
