#include <kernel/arch.h>
#include <kernel/mm.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/virtio.h>
#include <narc/abi.h>

#define GICD_BASE 0x08000000ULL
#define GICC_BASE 0x08010000ULL
#define TIMER_IRQ 27
#define SERIAL_IRQ 33
#define RTC_BASE 0x09010000ULL

extern char aarch64_vectors[];

static volatile uint32_t *gicd;
static volatile uint32_t *gicc;
static uint64_t timer_ticks;
static uint64_t clock_mult;

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
    timer_ticks = frequency / TIME_HZ;
    clock_mult = (NSEC_PER_SEC << 32) / frequency;
    timer_reset();
}

uint64_t arch_clock_ns(void) {
    uint64_t count;
    __asm__ volatile ("isb; mrs %0, cntvct_el0" : "=r"(count));
    return (uint64_t)(((unsigned __int128)count * clock_mult) >> 32);
}

int64_t arch_rtc_seconds(void) {
    volatile uint32_t *rtc = phys_to_virt(RTC_BASE);
    vmm_map_kernel((uint64_t)(uintptr_t)rtc, RTC_BASE, VMM_WRITE | VMM_DEVICE);
    return rtc[0];
}

void arch_probe_devices(void) {
    virtio_mmio_probe(0x0a000000ULL, 0x200, 32);
}

void arch_keyboard_init(void) {
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

uint64_t arch_syscall_status(const struct task_frame *frame) {
    return frame->x[1];
}

void arch_syscall_restart(struct task_frame *frame, const uint64_t saved[4]) {
    frame->x[0] = saved[1];
    frame->x[1] = saved[2];
    frame->pc -= 4;
}

uint64_t arch_frame_sp(const struct task_frame *frame) {
    return frame->sp;
}

int arch_signal_enter(struct task_frame *frame, uint64_t handler, uint64_t sp,
                      uint64_t restorer, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    frame->sp = sp;
    frame->pc = handler;
    frame->x[0] = arg0;
    frame->x[1] = arg1;
    frame->x[2] = arg2;
    frame->x[30] = restorer;
    return 0;
}

int arch_frame_sanitize(struct task_frame *frame, void *state) {
    (void)state;
    frame->pstate &= 0xf0000000ULL;
    return 0;
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

void arch_serial_irq_init(void) {
    volatile uint8_t *bytes = (volatile uint8_t *)gicd;
    bytes[0x400 + SERIAL_IRQ] = 0xa0;
    bytes[0x800 + SERIAL_IRQ] = 1;
    gicd[0x100 / 4 + SERIAL_IRQ / 32] = 1U << (SERIAL_IRQ % 32);
}

static int fault_signal(uint64_t esr) {
    switch (esr >> 26) {
    case 0x20: case 0x24:
        return (esr & 0x3f) == 0x21 ? NARC_SIGBUS : NARC_SIGSEGV;
    case 0x22: case 0x26:
        return NARC_SIGBUS;
    case 0x2c:
        return NARC_SIGFPE;
    case 0x30: case 0x32: case 0x34: case 0x3c:
        return NARC_SIGTRAP;
    default:
        return NARC_SIGILL;
    }
}

void aarch64_sync(struct task_frame *frame) {
    uint64_t esr, far;
    if (current_el() == 2) __asm__ volatile ("mrs %0, esr_el2; mrs %1, far_el2" : "=r"(esr), "=r"(far));
    else __asm__ volatile ("mrs %0, esr_el1; mrs %1, far_el1" : "=r"(esr), "=r"(far));
    if ((esr >> 26) == 0x15) {
        syscall_dispatch(frame);
        return;
    }
    if (arch_frame_from_user(frame)) {
        unsigned ec = (unsigned)(esr >> 26), status = (unsigned)(esr & 0x3f);
        int page_fault = (ec == 0x20 || ec == 0x24) &&
                         ((status & 0x3c) == 0x04 || (status & 0x3c) == 0x0c);
        int write = ec == 0x24 && ((esr >> 6) & 1);
        if (page_fault && vmm_resolve(vmm_space_current(), far, write) == 0) {
            user_return(frame);
            return;
        }
        signal_force(fault_signal(esr), far);
        user_return(frame);
        return;
    }
    serial_puts("[panic] esr=");
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
    if (irq >= 1020) return;
    if (irq == TIMER_IRQ) {
        timer_reset();
        time_tick();
    } else if (irq == SERIAL_IRQ) {
        serial_interrupt();
    }
    gicc[0x10 / 4] = irq;
    if (arch_frame_from_user(frame)) user_return(frame);
}
