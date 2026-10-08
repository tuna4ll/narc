#include <kernel/idt.h>
#include <kernel/mm.h>
#include <kernel/arch.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <stdint.h>

#define IA32_APIC_BASE 0x1bu

extern void timer_interrupt(void);
static volatile uint32_t *lapic;

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void timer_init(void) {
    idt_set_gate(32, timer_interrupt);
    uint64_t base = rdmsr(IA32_APIC_BASE);
    uint64_t phys = base & 0xfffff000ULL;
    lapic = phys_to_virt(phys);
    vmm_map_kernel((uint64_t)(uintptr_t)lapic, phys, VMM_WRITE);
    lapic[0xf0 / 4] = 0x1ff;
    lapic[0x3e0 / 4] = 3;
    lapic[0x320 / 4] = 32 | (1u << 17);
    lapic[0x380 / 4] = 10000000;
}

void timer_dispatch(struct task_frame *frame) {
    lapic[0xb0 / 4] = 0;
    sched_tick();
    if (arch_frame_from_user(frame)) sched_user_return();
}
