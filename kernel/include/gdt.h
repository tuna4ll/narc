#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define KERNEL_CODE_SEG 0x08
#define KERNEL_DATA_SEG 0x10
#define USER_DATA_SEG   0x18
#define USER_CODE_SEG   0x20
#define TSS_SEG         0x28

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void gdt_init(void);
void gdt_set_tss(uint64_t base, uint32_t limit);

#endif
