 /* 
 
 */


#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "pmm.h"

static uint8_t *bitmap = NULL;

static uint64_t bitmap_bytes = 0;
static uint64_t bitmap_pages = 0;
static uint64_t bitmap_phys = 0;

static uint64_t tracked_pages = 0;
static uint64_t total_pages = 0;
static uint64_t usable_bytes = 0;
static uint64_t free_pages = 0;

static bool initialized = false;

static void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (uint8_t)(1u << (page % 8));
}

static void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= (uint8_t)~(1u << (page % 8));
}

static bool bitmap_test(uint64_t page) {
    return (bitmap[page / 8] & (1u << (page % 8))) != 0;
}

static bool pmm_usable_page(uint64_t page) {
    return page < tracked_pages;
}

bool pmm_init(
    struct limine_memmap_response *memmap,
    uint64_t hhdm_offset
) {
    if (initialized || memmap == NULL) {
        return false;
    }

    uint64_t highest_address = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry == NULL ||
            entry->length > UINT64_MAX - entry->base) {
            return false;
        }

        uint64_t end = entry->base + entry->length;

        if (end > highest_address) {
            highest_address = end;
        }
    }

    if (highest_address == 0) {
        return false;
    }

    tracked_pages = highest_address / PAGE_SIZE;

    if (highest_address % PAGE_SIZE != 0) {
        tracked_pages++;
    }

    if (tracked_pages > UINT64_MAX - 7) {
        return false;
    }

    bitmap_bytes = (tracked_pages + 7) / 8;

    if (bitmap_bytes > UINT64_MAX - (PAGE_SIZE - 1)) {
        return false;
    }

    bitmap_pages =
        (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t bitmap_size = bitmap_pages * PAGE_SIZE;

    bool found = false;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        if (entry->length >= bitmap_size) {
            bitmap_phys = entry->base;
            found = true;
            break;
        }
    }

    if (!found || bitmap_phys > UINT64_MAX - hhdm_offset) {
        return false;
    }

    bitmap = (uint8_t *)(uintptr_t)(
        bitmap_phys + hhdm_offset
    );

    for (uint64_t i = 0; i < bitmap_bytes; i++) {
        bitmap[i] = 0xFF;
    }

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        usable_bytes += entry->length;

        uint64_t start_page = entry->base / PAGE_SIZE;
        uint64_t page_count = entry->length / PAGE_SIZE;

        for (uint64_t j = 0; j < page_count; j++) {
            uint64_t page = start_page + j;

            if (page < tracked_pages) {
                bitmap_clear(page);
                total_pages++;
                free_pages++;
            }
        }
    }

    uint64_t bitmap_start_page = bitmap_phys / PAGE_SIZE;

    for (uint64_t i = 0; i < bitmap_pages; i++) {
        uint64_t page = bitmap_start_page + i;

        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_pages--;
        }
    }

    initialized = true;
    return true;
}

uint64_t pmm_alloc_page(void) {
    if (!initialized) {
        return 0;
    }

    for (uint64_t page = 1; page < tracked_pages; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_pages--;

            return page * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_page(uint64_t physical_address) {
    if (!initialized ||
        physical_address % PAGE_SIZE != 0) {
        return;
    }

    uint64_t page = physical_address / PAGE_SIZE;

    if (!pmm_usable_page(page)) {
        return;
    }

    uint64_t bitmap_start = bitmap_phys / PAGE_SIZE;

    if (page >= bitmap_start &&
        page - bitmap_start < bitmap_pages) {
        return;
    }

    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

uint64_t pmm_get_total_pages(void) {
    return total_pages;
}

uint64_t pmm_get_free_pages(void) {
    return free_pages;
}

uint64_t pmm_get_usable_bytes(void) {
    return usable_bytes;
}
