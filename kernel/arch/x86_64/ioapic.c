#include <kernel/arch.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/serial.h>
#include <kernel/timer.h>

#define IOAPIC_BASE 0xfec00000ULL
#define SERIAL_IRQ 4
#define SERIAL_VECTOR 36

static volatile uint32_t *ioapic;

static void ioapic_write(uint32_t reg, uint32_t value) {
    ioapic[0] = reg;
    ioapic[4] = value;
}

static void ioapic_route(unsigned irq, uint8_t vector) {
    ioapic_write(0x10 + irq * 2 + 1, (uint32_t)lapic_id() << 24);
    ioapic_write(0x10 + irq * 2, vector);
}

static void serial_dispatch(struct task_frame *frame) {
    (void)frame;
    serial_interrupt();
    lapic_eoi();
}

void arch_serial_irq_init(void) {
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
    ioapic = phys_to_virt(IOAPIC_BASE);
    vmm_map_kernel((uint64_t)(uintptr_t)ioapic, IOAPIC_BASE, VMM_WRITE | VMM_DEVICE);
    idt_set_handler(SERIAL_VECTOR, serial_dispatch);
    ioapic_route(SERIAL_IRQ, SERIAL_VECTOR);
}
