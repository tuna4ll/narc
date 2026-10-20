#include <kernel/arch.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/serial.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/virtio.h>
#include <narc/abi.h>

#define PLIC_BASE 0x0c000000ULL
#define PLIC_CONTEXT 1
#define SERIAL_IRQ 10
#define RTC_BASE 0x00101000ULL
#define TIMEBASE_HZ 10000000ULL

extern char riscv_trap_entry[];

static volatile uint32_t *plic;

static const uint64_t timer_ticks = TIMEBASE_HZ / TIME_HZ;

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

uint64_t arch_clock_ns(void) {
    uint64_t now;
    __asm__ volatile ("rdtime %0" : "=r"(now));
    return now * (NSEC_PER_SEC / TIMEBASE_HZ);
}

int64_t arch_rtc_seconds(void) {
    volatile uint32_t *rtc = phys_to_virt(RTC_BASE);
    vmm_map_kernel((uint64_t)(uintptr_t)rtc, RTC_BASE, VMM_WRITE | VMM_DEVICE);
    uint64_t low = rtc[0];
    uint64_t high = rtc[1];
    return (int64_t)(((high << 32) | low) / NSEC_PER_SEC);
}

void arch_probe_devices(void) {
    virtio_mmio_probe(0x10001000ULL, 0x1000, 8);
}

void arch_keyboard_init(void) {
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

uint64_t arch_syscall_status(const struct task_frame *frame) {
    return frame->x[11];
}

void arch_syscall_restart(struct task_frame *frame, const uint64_t saved[4]) {
    frame->x[10] = saved[1];
    frame->x[11] = saved[2];
    frame->pc -= 4;
}

uint64_t arch_frame_sp(const struct task_frame *frame) {
    return frame->x[2];
}

int arch_signal_enter(struct task_frame *frame, uint64_t handler, uint64_t sp,
                      uint64_t restorer, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    frame->x[2] = sp;
    frame->pc = handler;
    frame->x[10] = arg0;
    frame->x[11] = arg1;
    frame->x[12] = arg2;
    frame->x[1] = restorer;
    return 0;
}

int arch_frame_sanitize(struct task_frame *frame, void *state) {
    (void)state;
    frame->x[0] = 0;
    frame->status = (1ULL << 5) | (3ULL << 13);
    return 0;
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

static volatile uint32_t *plic_reg(uint64_t offset) {
    return plic + offset / sizeof(uint32_t);
}

void arch_serial_irq_init(void) {
    static const uint64_t pages[] = { 0, 0x2000, 0x200000 + PLIC_CONTEXT * 0x1000 };
    plic = phys_to_virt(PLIC_BASE);
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
        vmm_map_kernel((uint64_t)(uintptr_t)plic + pages[i], PLIC_BASE + pages[i],
                       VMM_WRITE | VMM_DEVICE);
    *plic_reg(SERIAL_IRQ * 4) = 1;
    *plic_reg(0x2000 + PLIC_CONTEXT * 0x80) |= 1U << SERIAL_IRQ;
    *plic_reg(0x200000 + PLIC_CONTEXT * 0x1000) = 0;
    __asm__ volatile ("csrs sie, %0" : : "r"(1ULL << 9));
}

static void external_interrupt(void) {
    volatile uint32_t *claim = plic_reg(0x200004 + PLIC_CONTEXT * 0x1000);
    uint32_t irq = *claim;
    if (irq == SERIAL_IRQ) serial_interrupt();
    if (irq) *claim = irq;
}

static int fault_signal(uint64_t cause) {
    switch (cause) {
    case 0: case 4: case 6: return NARC_SIGBUS;
    case 2:                 return NARC_SIGILL;
    case 3:                 return NARC_SIGTRAP;
    default:                return NARC_SIGSEGV;
    }
}

void riscv_trap(struct task_frame *frame) {
    uint64_t cause;
    __asm__ volatile ("csrr %0, scause" : "=r"(cause));
    if (cause == ((1ULL << 63) | 5)) {
        timer_reset();
        time_tick();
        if (arch_frame_from_user(frame)) user_return(frame);
        return;
    }
    if (cause == ((1ULL << 63) | 9)) {
        external_interrupt();
        if (arch_frame_from_user(frame)) user_return(frame);
        return;
    }
    if (cause == 8) {
        frame->pc += 4;
        syscall_dispatch(frame);
        return;
    }
    uint64_t tval;
    __asm__ volatile ("csrr %0, stval" : "=r"(tval));
    if (arch_frame_from_user(frame)) {
        if ((cause == 12 || cause == 13 || cause == 15) &&
            vmm_resolve(vmm_space_current(), tval, cause == 15) == 0) {
            user_return(frame);
            return;
        }
        signal_force(fault_signal(cause), tval);
        user_return(frame);
        return;
    }
    serial_puts("[panic] scause=");
    serial_puthex(cause);
    serial_puts(" sepc=");
    serial_puthex(frame->pc);
    serial_puts(" stval=");
    serial_puthex(tval);
    serial_putc('\n');
    arch_halt();
}
