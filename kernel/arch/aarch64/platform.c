#include <kernel/arch.h>
#include <kernel/mm.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/sched.h>

#define GICD_BASE 0x08000000ULL
#define GICC_BASE 0x08010000ULL
#define TIMER_IRQ 27

extern char aarch64_vectors[];

static volatile uint32_t *gicd;
static volatile uint32_t *gicc;
static uint64_t timer_ticks;

static unsigned current_el(void) {
    uint64_t value;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(value));
    return (unsigned)(value >> 2);
}

static void timer_reset(void) {
    __asm__ volatile ("msr cntv_tval_el0, %0; msr cntv_ctl_el0, %1; isb"
                      : : "r"(timer_ticks), "r"(1ULL) : "memory");
}

void arch_init(uint64_t kernel_stack) {
    (void)kernel_stack;
    __asm__ volatile ("mov x9, sp; msr spsel, #1; mov sp, x9" ::: "x9", "memory");
    uint64_t mair;
    if (current_el() == 2) {
        __asm__ volatile ("mrs %0, mair_el2" : "=r"(mair));
        mair &= ~(0xffULL << 56);
        __asm__ volatile ("msr mair_el2, %0; msr vbar_el2, %1; isb"
                          : : "r"(mair), "r"(aarch64_vectors) : "memory");
        uint64_t cptr;
        __asm__ volatile ("mrs %0, cptr_el2" : "=r"(cptr));
        cptr &= ~(1ULL << 10);
        __asm__ volatile ("msr cptr_el2, %0; isb" : : "r"(cptr) : "memory");
    } else {
        __asm__ volatile ("mrs %0, mair_el1" : "=r"(mair));
        mair &= ~(0xffULL << 56);
        __asm__ volatile ("msr mair_el1, %0; msr vbar_el1, %1; isb"
                          : : "r"(mair), "r"(aarch64_vectors) : "memory");
        uint64_t cpacr;
        __asm__ volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
        cpacr |= 3ULL << 20;
        __asm__ volatile ("msr cpacr_el1, %0; isb" : : "r"(cpacr) : "memory");
    }

    for (uint64_t phys = GICD_BASE; phys < GICD_BASE + 0x2000; phys += PAGE_SIZE)
        vmm_map_kernel((uint64_t)(uintptr_t)phys_to_virt(phys), phys, VMM_WRITE | VMM_DEVICE);
    vmm_map_kernel((uint64_t)(uintptr_t)phys_to_virt(GICC_BASE), GICC_BASE,
                   VMM_WRITE | VMM_DEVICE);
    gicd = phys_to_virt(GICD_BASE);
    gicc = phys_to_virt(GICC_BASE);
    gicd[0] = 1;
    gicd[0x100 / 4 + TIMER_IRQ / 32] = 1U << (TIMER_IRQ % 32);
    gicc[1] = 0xff;
    gicc[0] = 1;

    uint64_t frequency;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(frequency));
    timer_ticks = frequency / 100;
    timer_reset();
}

void arch_halt(void) {
    for (;;) __asm__ volatile ("msr daifset, #0xf; wfi");
}

void arch_set_tls(uint64_t value) {
    __asm__ volatile ("msr tpidr_el0, %0" : : "r"(value));
}

void arch_task_frame_init(struct task_frame *frame) {
    memset(frame, 0, sizeof(*frame));
}

void arch_task_set_entry(struct task_frame *frame, uint64_t pc, uint64_t sp) {
    frame->pc = pc;
    frame->sp = sp;
}

void arch_task_state_init(void *state) {
    memset(state, 0, ARCH_STATE_SIZE);
}

uint64_t arch_syscall_number(const struct task_frame *frame) {
    return frame->x[8];
}

uint64_t arch_syscall_arg(const struct task_frame *frame, unsigned index) {
    return index < 6 ? frame->x[index] : 0;
}

void arch_syscall_return(struct task_frame *frame, uint64_t value) {
    frame->x[0] = value;
}

void arch_syscall_return2(struct task_frame *frame, uint64_t value, uint64_t status) {
    frame->x[0] = value;
    frame->x[1] = status;
}

int arch_frame_from_user(const struct task_frame *frame) {
    return (frame->pstate & 0xf) == 0;
}

uint64_t arch_context_init(uint64_t stack_top, void (*entry)(void)) {
    uint64_t *sp = (uint64_t *)(uintptr_t)((stack_top & ~0xfULL) - 96);
    memset(sp, 0, 96);
    sp[11] = (uint64_t)(uintptr_t)entry;
    return (uint64_t)(uintptr_t)sp;
}

void arch_set_kernel_stack(uint64_t top) {
    (void)top;
}

void aarch64_sync(struct task_frame *frame) {
    uint64_t esr, far;
    if (current_el() == 2) __asm__ volatile ("mrs %0, esr_el2; mrs %1, far_el2" : "=r"(esr), "=r"(far));
    else __asm__ volatile ("mrs %0, esr_el1; mrs %1, far_el1" : "=r"(esr), "=r"(far));
    if ((esr >> 26) == 0x15) {
        syscall_dispatch(frame);
        return;
    }
    serial_puts("[fault] esr=");
    serial_puthex(esr);
    serial_puts(" elr=");
    serial_puthex(frame->pc);
    serial_puts(" far=");
    serial_puthex(far);
    serial_putc('\n');
    arch_halt();
}

void aarch64_irq(struct task_frame *frame) {
    uint32_t irq = gicc[0x0c / 4] & 0x3ff;
    if (irq == TIMER_IRQ) {
        timer_reset();
        sched_tick();
    }
    gicc[0x10 / 4] = irq;
    if (arch_frame_from_user(frame)) sched_user_return();
}
