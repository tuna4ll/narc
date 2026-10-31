#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include "boot.h"

#define PTE_PRESENT (1ull << 0)
#define PTE_WRITE   (1ull << 1)
#define PTE_USER    (1ull << 2)
#define PTE_PWT     (1ull << 3)
#define PTE_HUGE    (1ull << 7)
#define PTE_PAT     (1ull << 7)
#define PTE_NX      (1ull << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull

bool vmm_init(struct boot_info *boot);
uint64_t vmm_read_cr3(void);
void vmm_write_cr3(uint64_t cr3);
uint64_t vmm_virt_to_phys(uint64_t virt);
bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vmm_unmap_page(uint64_t virt);

#endif
