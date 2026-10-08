#include <kernel/gdt.h>
#include <kernel/string.h>
#include <stdint.h>

struct __attribute__((packed)) gdtr {
    uint16_t limit;
    uint64_t base;
};

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static uint64_t gdt[7];
static struct tss64 tss;

extern void gdt_load(const struct gdtr *gdtr);
extern void tss_load(uint16_t selector);

static void set_tss_descriptor(uintptr_t base, uint32_t limit) {
    uint64_t low = 0;
    low |= (limit & 0xffffULL);
    low |= (base & 0xffffffULL) << 16;
    low |= 0x89ULL << 40;
    low |= ((uint64_t)(limit >> 16) & 0xfULL) << 48;
    low |= ((uint64_t)(base >> 24) & 0xffULL) << 56;
    gdt[5] = low;
    gdt[6] = (uint64_t)(base >> 32);
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(uint64_t rsp0) {
    memset(gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt[1] = 0x00af9a000000ffffULL; /* kernel code */
    gdt[2] = 0x00cf92000000ffffULL; /* kernel data */
    gdt[3] = 0x00cff2000000ffffULL; /* user data */
    gdt[4] = 0x00affa000000ffffULL; /* user code */

    tss.rsp0 = rsp0;
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor((uintptr_t)&tss, sizeof(tss) - 1);

    struct gdtr ptr = { .limit = sizeof(gdt) - 1, .base = (uint64_t)(uintptr_t)gdt };
    gdt_load(&ptr);
    tss_load(0x28);
}
