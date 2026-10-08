#include <kernel/mm.h>

#define DESC_VALID (1ULL << 0)
#define DESC_TABLE (1ULL << 1)
#define DESC_AF    (1ULL << 10)
#define DESC_USER  (1ULL << 6)
#define DESC_RO    (1ULL << 7)
#define DESC_SH    (3ULL << 8)
#define DESC_PXN   (1ULL << 53)
#define DESC_UXN   (1ULL << 54)
#define DESC_DEVICE (7ULL << 2)
#define ADDR_MASK  0x0000fffffffff000ULL

static struct address_space *current_space;

static unsigned current_el(void) {
    uint64_t value;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(value));
    return (unsigned)(value >> 2);
}

static uint64_t read_ttbr0(void) {
    uint64_t value;
    if (current_el() == 2) __asm__ volatile ("mrs %0, ttbr0_el2" : "=r"(value));
    else __asm__ volatile ("mrs %0, ttbr0_el1" : "=r"(value));
    return value & ADDR_MASK;
}

static uint64_t read_ttbr1(void) {
    uint64_t value;
    if (current_el() == 2) __asm__ volatile ("mrs %0, ttbr1_el2" : "=r"(value));
    else __asm__ volatile ("mrs %0, ttbr1_el1" : "=r"(value));
    return value & ADDR_MASK;
}

static void flush_tlb(void) {
    __asm__ volatile ("dsb ishst" ::: "memory");
    if (current_el() == 2) __asm__ volatile ("tlbi alle2is");
    else __asm__ volatile ("tlbi vmalle1is");
    __asm__ volatile ("dsb ish; isb" ::: "memory");
}

static uint64_t *next_table(uint64_t *table, size_t index, int create) {
    uint64_t entry = table[index];
    if (!(entry & DESC_VALID)) {
        if (!create) return 0;
        uint64_t phys = pmm_alloc_page();
        if (!phys) return 0;
        table[index] = phys | DESC_VALID | DESC_TABLE;
        return phys_to_virt(phys);
    }
    if (!(entry & DESC_TABLE)) return 0;
    return phys_to_virt(entry & ADDR_MASK);
}

static uint64_t *get_pte(uint64_t root, uint64_t virt, int create) {
    uint64_t *table = phys_to_virt(root);
    for (int shift = 39; shift > 12; shift -= 9) {
        table = next_table(table, (virt >> shift) & 0x1ff, create);
        if (!table) return 0;
    }
    return &table[(virt >> 12) & 0x1ff];
}

static int map_page(uint64_t root, uint64_t virt, uint64_t phys, uint64_t flags, int user) {
    uint64_t *pte = get_pte(root, virt, 1);
    if (!pte) return -1;
    uint64_t bits = DESC_VALID | DESC_TABLE | DESC_AF | DESC_SH;
    if (user) {
        bits |= DESC_USER | DESC_PXN;
        if (!(flags & VMM_WRITE)) bits |= DESC_RO;
    } else if (flags & VMM_DEVICE) {
        bits |= DESC_DEVICE | DESC_PXN | DESC_UXN;
    }
    *pte = (phys & ADDR_MASK) | bits;
    flush_tlb();
    return 0;
}

int vmm_space_create(struct address_space *space) {
    space->root = pmm_alloc_page();
    return space->root ? 0 : -1;
}

static int clone_table(uint64_t dst_phys, uint64_t src_phys, unsigned level) {
    uint64_t *dst = phys_to_virt(dst_phys);
    uint64_t *src = phys_to_virt(src_phys);
    for (size_t i = 0; i < 512; i++) {
        uint64_t entry = src[i];
        if (!(entry & DESC_VALID)) continue;
        uint64_t page = pmm_alloc_page();
        if (!page) return -1;
        dst[i] = page | (entry & ~ADDR_MASK);
        if (level == 1 || !(entry & DESC_TABLE))
            __builtin_memcpy(phys_to_virt(page), phys_to_virt(entry & ADDR_MASK), PAGE_SIZE);
        else if (clone_table(page, entry & ADDR_MASK, level - 1) != 0)
            return -1;
    }
    return 0;
}

int vmm_space_clone(struct address_space *dst, struct address_space *src) {
    if (vmm_space_create(dst) != 0) return -1;
    if (clone_table(dst->root, src->root, 4) == 0) return 0;
    vmm_space_destroy(dst);
    return -1;
}

void vmm_space_boot(struct address_space *space) {
    space->root = read_ttbr0();
}

void vmm_space_activate(struct address_space *space) {
    current_space = space;
    if (current_el() == 2) __asm__ volatile ("msr ttbr0_el2, %0; isb" : : "r"(space->root) : "memory");
    else __asm__ volatile ("msr ttbr0_el1, %0; isb" : : "r"(space->root) : "memory");
    flush_tlb();
}

static void free_table(uint64_t phys, unsigned level) {
    uint64_t *table = phys_to_virt(phys);
    for (size_t i = 0; i < 512; i++) {
        if (!(table[i] & DESC_VALID)) continue;
        uint64_t child = table[i] & ADDR_MASK;
        if (level == 1) pmm_free_page(child);
        else free_table(child, level - 1);
    }
    pmm_free_page(phys);
}

void vmm_space_destroy(struct address_space *space) {
    if (!space->root || read_ttbr0() == space->root) return;
    free_table(space->root, 4);
    space->root = 0;
}

struct address_space *vmm_space_current(void) {
    return current_space;
}

int vmm_map_user(struct address_space *space, uint64_t virt, uint64_t phys, uint64_t flags) {
    return map_page(space->root, virt, phys, flags, 1);
}

int vmm_map_kernel(uint64_t virt, uint64_t phys, uint64_t flags) {
    return map_page(read_ttbr1(), virt, phys, flags, 0);
}

uint64_t vmm_unmap_kernel(uint64_t virt) {
    uint64_t *pte = get_pte(read_ttbr1(), virt, 0);
    if (!pte || !(*pte & DESC_VALID)) return 0;
    uint64_t phys = *pte & ADDR_MASK;
    *pte = 0;
    flush_tlb();
    return phys;
}

void vmm_kernel_window(uint64_t *base, uint64_t *size) {
    *base = 0xffffe00000000000ULL;
    *size = 1ULL << 39;
}

int vmm_protect_user(struct address_space *space, uint64_t virt, uint64_t flags) {
    uint64_t *pte = get_pte(space->root, virt, 0);
    if (!pte || !(*pte & DESC_VALID) || !(*pte & DESC_USER)) return -1;
    if (flags & VMM_WRITE) *pte &= ~DESC_RO;
    else *pte |= DESC_RO;
    flush_tlb();
    return 0;
}

int vmm_unmap_user(struct address_space *space, uint64_t virt) {
    uint64_t *pte = get_pte(space->root, virt, 0);
    if (!pte || !(*pte & DESC_VALID) || !(*pte & DESC_USER)) return -1;
    uint64_t phys = *pte & ADDR_MASK;
    *pte = 0;
    flush_tlb();
    pmm_free_page(phys);
    return 0;
}

uint64_t vmm_user_phys(struct address_space *space, uint64_t virt) {
    uint64_t *pte = get_pte(space->root, virt, 0);
    if (!pte || !(*pte & DESC_VALID) || !(*pte & DESC_USER)) return 0;
    return (*pte & ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

uint64_t vmm_user_end(void) {
    return 0x0001000000000000ULL;
}

int vmm_user_range_ok(struct address_space *space, uint64_t virt, uint64_t len, int write) {
    if (!len) return 1;
    if (virt >= vmm_user_end() || len > vmm_user_end() - virt) return 0;
    uint64_t last = (virt + len - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t page = virt & ~(PAGE_SIZE - 1);; page += PAGE_SIZE) {
        uint64_t *pte = get_pte(space->root, page, 0);
        if (!pte || !(*pte & DESC_VALID) || !(*pte & DESC_USER)) return 0;
        if (write && (*pte & DESC_RO)) return 0;
        if (page == last) break;
    }
    return 1;
}
