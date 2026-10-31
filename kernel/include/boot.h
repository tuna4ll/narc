#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <stdbool.h>
#include <stdint.h>

enum mem_region_type {
    MEM_REGION_USABLE,
    MEM_REGION_RESERVED,
    MEM_REGION_ACPI_RECLAIMABLE,
    MEM_REGION_ACPI_NVS,
    MEM_REGION_BAD,
    MEM_REGION_BOOTLOADER_RECLAIMABLE,
    MEM_REGION_KERNEL,
    MEM_REGION_FRAMEBUFFER,
    MEM_REGION_RESERVED_MAPPED,
};

struct mem_region {
    uint64_t base;
    uint64_t length;
    enum mem_region_type type;
};

struct boot_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct boot_info {
    const struct mem_region *memmap;
    uint64_t memmap_count;
    uint64_t hhdm_offset;
    uint64_t kernel_phys_base;
    uint64_t kernel_virt_base;
    struct boot_framebuffer framebuffer;
};

bool boot_init(struct boot_info *info);

#endif 
