#include <stdint.h>

#include "gdt.h"
#include "kprint.h"

static struct gdt_entry gdt[7];
static struct gdt_ptr gp;

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].access = access;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base_high = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set_entry(3, 0, 0xFFFFF, 0xF2, 0xC0);
    gdt_set_entry(4, 0, 0xFFFFF, 0xFA, 0xA0);

    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint64_t)&gdt;

    __asm__ volatile ("lgdt %0" : : "m"(gp));

    __asm__ volatile (
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw $0, %%ax\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        : : : "rax", "memory"
    );

    kprint("GDT initialized!\n");
}

void gdt_set_tss(uint64_t base, uint32_t limit) {
    gdt_set_entry(5, (uint32_t)base, limit, 0x89, 0x00);

    uint32_t *high = (uint32_t *)&gdt[6];
    high[0] = base >> 32;
    high[1] = 0;
}
