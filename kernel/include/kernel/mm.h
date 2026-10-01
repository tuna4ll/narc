#pragma once
#include <stddef.h>
#include <stdint.h>
#include "limine.h"

#define PAGE_SIZE 4096ULL
#define VMM_WRITE (1ULL << 1)
#define VMM_DEVICE (1ULL << 2)

struct address_space {
    uint64_t root;
};

void mm_init(struct limine_memmap_response *map, uint64_t hhdm_offset);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys);
void *phys_to_virt(uint64_t phys);
int vmm_space_create(struct address_space *space);
int vmm_space_clone(struct address_space *dst, struct address_space *src);
void vmm_space_activate(struct address_space *space);
void vmm_space_destroy(struct address_space *space);
struct address_space *vmm_space_current(void);
int vmm_map_user(struct address_space *space, uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_protect_user(struct address_space *space, uint64_t virt, uint64_t flags);
int vmm_unmap_user(struct address_space *space, uint64_t virt);
int vmm_user_range_ok(struct address_space *space, uint64_t virt, uint64_t len, int write);
uint64_t vmm_user_phys(struct address_space *space, uint64_t virt);
int vmm_map_kernel(uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vmm_unmap_kernel(uint64_t virt);
void vmm_kernel_window(uint64_t *base, uint64_t *size);
