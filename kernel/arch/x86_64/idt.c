#include <kernel/idt.h>
#include <kernel/string.h>
#include <stdint.h>

struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry idt[256];
extern const uint64_t interrupt_stubs[IDT_VECTORS];

static void set_gate(unsigned vector, uint64_t addr) {
    idt[vector] = (struct idt_entry){
        .offset_low = addr,
        .selector = 0x08,
        .type_attr = 0x8e,
        .offset_mid = addr >> 16,
        .offset_high = addr >> 32,
    };
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    for (unsigned vector = 0; vector < IDT_VECTORS; vector++)
        set_gate(vector, interrupt_stubs[vector]);

    struct idtr ptr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(ptr));
}
