#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>

#include "boot.h"

#define PAGE_SIZE 4096

bool pmm_init(
    const struct mem_region *regions,
    uint64_t region_count,
    uint64_t hhdm_offset
);

uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t physical_address);

uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_usable_bytes(void);

#endif /* PMM_H */
