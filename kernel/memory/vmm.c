#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vmm.h"
#include "pmm.h"
#include "mem.h"

#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PD_INDEX(a)   (((a) >> 21) & 0x1FF)
#define PT_INDEX(a)   (((a) >> 12) & 0x1FF)

#define MB2 0x200000

extern char __kernel_start[], __text_start[], __rodata_start[], __data_start[], __kernel_end[];

static uint64_t hhdm = 0;
static uint64_t kernel_pml4 = 0;

uint64_t vmm_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void vmm_write_cr3(uint64_t cr3) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static uint64_t *phys_to_table(uint64_t phys) {
    return (uint64_t *)(phys + hhdm);
}

uint64_t vmm_virt_to_phys(uint64_t virt) {
    uint64_t *pml4 = phys_to_table(vmm_read_cr3() & PTE_ADDR_MASK);

    uint64_t e = pml4[PML4_INDEX(virt)];
    if (!(e & PTE_PRESENT))
        return 0;

    uint64_t *pdpt = phys_to_table(e & PTE_ADDR_MASK);
    e = pdpt[PDPT_INDEX(virt)];
    if (!(e & PTE_PRESENT))
        return 0;
    if (e & PTE_HUGE)
        return (e & 0x000FFFFFC0000000ull) + (virt & 0x3FFFFFFF);

    uint64_t *pd = phys_to_table(e & PTE_ADDR_MASK);
    e = pd[PD_INDEX(virt)];
    if (!(e & PTE_PRESENT))
        return 0;
    if (e & PTE_HUGE)
        return (e & 0x000FFFFFFFE00000ull) + (virt & 0x1FFFFF);

    uint64_t *pt = phys_to_table(e & PTE_ADDR_MASK);
    e = pt[PT_INDEX(virt)];
    if (!(e & PTE_PRESENT))
        return 0;

    return (e & PTE_ADDR_MASK) + (virt & 0xFFF);
}

static uint64_t *get_next_table(uint64_t *table, uint64_t index) {
    if (table[index] & PTE_PRESENT) {
        if (table[index] & PTE_HUGE)
            return NULL;
        return phys_to_table(table[index] & PTE_ADDR_MASK);
    }

    uint64_t page = pmm_alloc_page();
    if (page == 0)
        return NULL;

    memset(phys_to_table(page), 0, PAGE_SIZE);

    table[index] = page | PTE_PRESENT | PTE_WRITE;
    return phys_to_table(page);
}

bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if ((virt & 0xFFF) || (phys & 0xFFF))
        return false;

    uint64_t *pml4 = phys_to_table(kernel_pml4);
    uint64_t *pdpt = get_next_table(pml4, PML4_INDEX(virt));
    if (!pdpt) return false;
    uint64_t *pd = get_next_table(pdpt, PDPT_INDEX(virt));
    if (!pd) return false;
    uint64_t *pt = get_next_table(pd, PD_INDEX(virt));
    if (!pt) return false;

    pt[PT_INDEX(virt)] = phys | flags | PTE_PRESENT;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return true;
}

static bool map_2mb(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = phys_to_table(kernel_pml4);
    uint64_t *pdpt = get_next_table(pml4, PML4_INDEX(virt));
    if (!pdpt) return false;
    uint64_t *pd = get_next_table(pdpt, PDPT_INDEX(virt));
    if (!pd) return false;

    if ((pd[PD_INDEX(virt)] & PTE_PRESENT) && !(pd[PD_INDEX(virt)] & PTE_HUGE))
        return false;

    pd[PD_INDEX(virt)] = phys | flags | PTE_HUGE | PTE_PRESENT;
    return true;
}

static bool map_hhdm_region(uint64_t start, uint64_t end) {
    uint64_t p = start;

    while (p < end) {
        if (p % MB2 == 0 && end - p >= MB2) {
            if (!map_2mb(p + hhdm, p, PTE_WRITE | PTE_NX))
                return false;
            p += MB2;
        } else {
            if (!vmm_map_page(p + hhdm, p, PTE_WRITE | PTE_NX))
                return false;
            p += PAGE_SIZE;
        }
    }
    return true;
}

static bool map_kernel_part(struct boot_info *boot, char *start, char *end, uint64_t flags) {
    for (uint64_t v = (uint64_t)start; v < (uint64_t)end; v += PAGE_SIZE) {
        uint64_t p = v - boot->kernel_virt_base + boot->kernel_phys_base;
        if (!vmm_map_page(v, p, flags))
            return false;
    }
    return true;
}

bool vmm_init(struct boot_info *boot) {
    hhdm = boot->hhdm_offset;

    kernel_pml4 = pmm_alloc_page();
    if (kernel_pml4 == 0)
        return false;

    uint64_t *pml4 = phys_to_table(kernel_pml4);
    memset(pml4, 0, PAGE_SIZE);

    for (int i = 256; i < 512; i++) {
        if (!get_next_table(pml4, i))
            return false;
    }

    for (uint64_t i = 0; i < boot->memmap_count; i++) {
        const struct mem_region *r = &boot->memmap[i];

        if (r->type == MEM_REGION_RESERVED || r->type == MEM_REGION_BAD)
            continue;

        uint64_t start = r->base & ~0xFFFull;
        uint64_t end = (r->base + r->length + 0xFFF) & ~0xFFFull;

        if (r->type == MEM_REGION_FRAMEBUFFER) {
            for (uint64_t p = start; p < end; p += PAGE_SIZE) {
                if (!vmm_map_page(p + hhdm, p, PTE_WRITE | PTE_NX | PTE_PAT | PTE_PWT))
                    return false;
            }
        } else {
            if (!map_hhdm_region(start, end))
                return false;
        }
    }

    if (!map_kernel_part(boot, __kernel_start, __text_start, PTE_WRITE | PTE_NX)) return false;
    if (!map_kernel_part(boot, __text_start, __rodata_start, 0)) return false;
    if (!map_kernel_part(boot, __rodata_start, __data_start, PTE_NX)) return false;
    if (!map_kernel_part(boot, __data_start, __kernel_end, PTE_WRITE | PTE_NX)) return false;

    vmm_write_cr3(kernel_pml4);
    return true;
}

static bool table_empty(uint64_t *t) {
    for (int i = 0; i < 512; i++)
        if (t[i]) return false;
    return true;
}

uint64_t vmm_unmap_page(uint64_t virt) {
    uint64_t *pml4 = phys_to_table(kernel_pml4);

    uint64_t e = pml4[PML4_INDEX(virt)];
    if (!(e & PTE_PRESENT)) return 0;
    uint64_t *pdpt = phys_to_table(e & PTE_ADDR_MASK);

    e = pdpt[PDPT_INDEX(virt)];
    if (!(e & PTE_PRESENT) || (e & PTE_HUGE)) return 0;
    uint64_t pd_phys = e & PTE_ADDR_MASK;
    uint64_t *pd = phys_to_table(pd_phys);

    e = pd[PD_INDEX(virt)];
    if (!(e & PTE_PRESENT) || (e & PTE_HUGE)) return 0;
    uint64_t pt_phys = e & PTE_ADDR_MASK;
    uint64_t *pt = phys_to_table(pt_phys);

    uint64_t page = pt[PT_INDEX(virt)];
    if (!(page & PTE_PRESENT)) return 0;

    pt[PT_INDEX(virt)] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");

    if (table_empty(pt)) {
        pd[PD_INDEX(virt)] = 0;
        pmm_free_page(pt_phys);

        if (table_empty(pd)) {
            pdpt[PDPT_INDEX(virt)] = 0;
            pmm_free_page(pd_phys);
        }
    }

    return page & PTE_ADDR_MASK;
}
