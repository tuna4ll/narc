#include <kernel/arch.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/mm.h>
#include <kernel/time.h>
#include <kernel/timer.h>
#include <stdint.h>

#define IA32_APIC_BASE 0x1bu
#define PIT_HZ 1193182ULL
#define CALIBRATE_MS 10

static volatile uint32_t *lapic;
static uint64_t tsc_base, tsc_mult;

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void timer_dispatch(struct task_frame *frame) {
    (void)frame;
    lapic_eoi();
    time_tick();
}

static void pit_wait(void) {
    uint16_t count = (uint16_t)(PIT_HZ * CALIBRATE_MS / 1000);
    outb(0x61, (uint8_t)((inb(0x61) & ~0x02) | 0x01));
    outb(0x43, 0xb0);
    outb(0x42, (uint8_t)count);
    outb(0x42, (uint8_t)(count >> 8));
    while (!(inb(0x61) & 0x20)) arch_cpu_relax();
}

void timer_init(void) {
    idt_set_handler(32, timer_dispatch);
    uint64_t base = rdmsr(IA32_APIC_BASE);
    uint64_t phys = base & 0xfffff000ULL;
    lapic = phys_to_virt(phys);
    vmm_map_kernel((uint64_t)(uintptr_t)lapic, phys, VMM_WRITE);
    lapic[0xf0 / 4] = 0x1ff;
    lapic[0x3e0 / 4] = 3;
    lapic[0x320 / 4] = 1u << 16;
    lapic[0x380 / 4] = 0xffffffffu;
    uint64_t tsc_start = rdtsc();
    pit_wait();
    uint64_t tsc_end = rdtsc();
    uint32_t elapsed = 0xffffffffu - lapic[0x390 / 4];
    uint64_t tsc_hz = (tsc_end - tsc_start) * (1000 / CALIBRATE_MS);
    tsc_mult = (NSEC_PER_SEC << 32) / tsc_hz;
    tsc_base = tsc_end;
    uint32_t period = elapsed * (1000 / CALIBRATE_MS) / TIME_HZ;
    lapic[0x320 / 4] = 32 | (1u << 17);
    lapic[0x380 / 4] = period ? period : 1;
}

uint64_t arch_clock_ns(void) {
    uint64_t delta = rdtsc() - tsc_base;
    return (uint64_t)(((unsigned __int128)delta * tsc_mult) >> 32);
}

void lapic_eoi(void) {
    lapic[0xb0 / 4] = 0;
}

uint8_t lapic_id(void) {
    return (uint8_t)(lapic[0x20 / 4] >> 24);
}
