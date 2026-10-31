#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include "boot.h"

#define MAX_MEMMAP_REGIONS 256

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request kernel_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static struct mem_region regions[MAX_MEMMAP_REGIONS];

static enum mem_region_type convert_type(uint64_t type) {
    switch (type) {
    case LIMINE_MEMMAP_USABLE:                 return MEM_REGION_USABLE;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return MEM_REGION_ACPI_RECLAIMABLE;
    case LIMINE_MEMMAP_ACPI_NVS:               return MEM_REGION_ACPI_NVS;
    case LIMINE_MEMMAP_BAD_MEMORY:             return MEM_REGION_BAD;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return MEM_REGION_BOOTLOADER_RECLAIMABLE;
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return MEM_REGION_KERNEL;
    case LIMINE_MEMMAP_FRAMEBUFFER:            return MEM_REGION_FRAMEBUFFER;
    case LIMINE_MEMMAP_RESERVED_MAPPED:        return MEM_REGION_RESERVED_MAPPED;
    default:                                   return MEM_REGION_RESERVED;
    }
}

bool boot_init(struct boot_info *info) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)
        || framebuffer_request.response == NULL
        || framebuffer_request.response->framebuffer_count < 1
        || memmap_request.response == NULL
        || hhdm_request.response == NULL
        || kernel_address_request.response == NULL) {
        return false;
    }

    struct limine_memmap_response *memmap = memmap_request.response;

    if (memmap->entry_count > MAX_MEMMAP_REGIONS) {
        return false;
    }

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry == NULL) {
            return false;
        }

        regions[i].base = entry->base;
        regions[i].length = entry->length;
        regions[i].type = convert_type(entry->type);
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    info->memmap = regions;
    info->memmap_count = memmap->entry_count;
    info->hhdm_offset = hhdm_request.response->offset;
    info->kernel_phys_base = kernel_address_request.response->physical_base;
    info->kernel_virt_base = kernel_address_request.response->virtual_base;
    info->framebuffer = (struct boot_framebuffer){
        .address = fb->address,
        .width = fb->width,
        .height = fb->height,
        .pitch = fb->pitch,
        .bpp = fb->bpp,
        .red_mask_size = fb->red_mask_size,
        .red_mask_shift = fb->red_mask_shift,
        .green_mask_size = fb->green_mask_size,
        .green_mask_shift = fb->green_mask_shift,
        .blue_mask_size = fb->blue_mask_size,
        .blue_mask_shift = fb->blue_mask_shift,
    };

    return true;
}
