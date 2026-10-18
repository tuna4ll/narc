#include <kernel/mm.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>

#if defined(__x86_64__)
#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_USER    (1ULL << 2)
#define PTE_PWT     (1ULL << 3)
#define PTE_PCD     (1ULL << 4)
#define PTE_HUGE    (1ULL << 7)
#define PTE_COW     (1ULL << 9)
#define ADDR_MASK   0x000ffffffffff000ULL
#endif

static struct limine_memmap_response *memmap;
static uint64_t hhdm;
static uint64_t region_index;
static uint64_t next_phys;
static uint64_t region_end;
static uint64_t free_head;
static struct spinlock pmm_lock = SPINLOCK_INIT;
static uint16_t *page_refs;
static uint64_t refs_base, refs_count;
static uint64_t reserved_base, reserved_end;
#if defined(__x86_64__)
static struct address_space *current_space;
#endif

static uint64_t align_up(uint64_t x, uint64_t a) {
    return (x + a - 1) & ~(a - 1);
}

static void reserve_refs(void) {
    uint64_t low = UINT64_MAX, high = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        if (e->base < low) low = e->base;
        if (e->base + e->length > high) high = e->base + e->length;
    }
    if (low >= high) return;
    refs_base = low & ~(PAGE_SIZE - 1);
    refs_count = (high - refs_base) / PAGE_SIZE;
    uint64_t bytes = align_up(refs_count * sizeof(uint16_t), PAGE_SIZE);
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        uint64_t base = align_up(e->base, PAGE_SIZE);
        if (e->type != LIMINE_MEMMAP_USABLE || base + bytes > e->base + e->length) continue;
        reserved_base = base;
        reserved_end = base + bytes;
        page_refs = phys_to_virt(base);
        memset(page_refs, 0, bytes);
        return;
    }
    refs_count = 0;
}

static uint16_t *page_ref(uint64_t phys) {
    if (!page_refs || phys < refs_base) return 0;
    uint64_t index = (phys - refs_base) / PAGE_SIZE;
    return index < refs_count ? &page_refs[index] : 0;
}

void mm_init(struct limine_memmap_response *map, uint64_t hhdm_offset) {
    memmap = map;
    hhdm = hhdm_offset;
    region_index = 0;
    next_phys = 0;
    region_end = 0;
    free_head = 0;
    reserve_refs();
}

void *phys_to_virt(uint64_t phys) {
    return (void *)(uintptr_t)(phys + hhdm);
}

static uint64_t take_page(void) {
    if (free_head) {
        uint64_t page = free_head;
        free_head = *(uint64_t *)phys_to_virt(page);
        return page;
    }

    for (;;) {
        if (next_phys && next_phys + PAGE_SIZE <= region_end) {
            uint64_t page = next_phys;
            next_phys += PAGE_SIZE;
            if (page >= reserved_base && page < reserved_end) continue;
            return page;
        }

        while (region_index < memmap->entry_count) {
            struct limine_memmap_entry *e = memmap->entries[region_index++];
            if (e->type != LIMINE_MEMMAP_USABLE || e->length < PAGE_SIZE) continue;
            next_phys = align_up(e->base, PAGE_SIZE);
            region_end = e->base + e->length;
            break;
        }

        if (!next_phys || next_phys + PAGE_SIZE > region_end) return 0;
    }
}

uint64_t pmm_alloc_page(void) {
    unsigned long flags = spin_lock_irqsave(&pmm_lock);
    uint64_t page = take_page();
    uint16_t *refs = page ? page_ref(page) : 0;
    if (refs) *refs = 1;
    spin_unlock_irqrestore(&pmm_lock, flags);
    if (page) memset(phys_to_virt(page), 0, PAGE_SIZE);
    return page;
}

void pmm_free_page(uint64_t phys) {
    if (!phys || (phys & (PAGE_SIZE - 1))) return;
    unsigned long flags = spin_lock_irqsave(&pmm_lock);
    uint16_t *refs = page_ref(phys);
    if (refs && *refs > 1) {
        (*refs)--;
    } else {
        if (refs) *refs = 0;
        *(uint64_t *)phys_to_virt(phys) = free_head;
        free_head = phys;
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

void pmm_page_ref(uint64_t phys) {
    unsigned long flags = spin_lock_irqsave(&pmm_lock);
    uint16_t *refs = page_ref(phys);
    if (refs) (*refs)++;
    spin_unlock_irqrestore(&pmm_lock, flags);
}

unsigned pmm_page_refs(uint64_t phys) {
    unsigned long flags = spin_lock_irqsave(&pmm_lock);
    uint16_t *refs = page_ref(phys);
    unsigned count = refs ? *refs : 1;
    spin_unlock_irqrestore(&pmm_lock, flags);
    return count;
}

#if defined(__x86_64__)
static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value & ADDR_MASK;
}

static uint64_t *next_table(uint64_t *table, size_t index, int create, int user) {
    uint64_t entry = table[index];
    if (!(entry & PTE_PRESENT)) {
        if (!create) return 0;
        uint64_t phys = pmm_alloc_page();
        if (!phys) return 0;
        uint64_t bits = PTE_PRESENT | PTE_WRITE;
        if (user) bits |= PTE_USER;
        table[index] = phys | bits;
        return phys_to_virt(phys);
    }

    if (entry & PTE_HUGE) return 0;
    if (user && create) table[index] |= PTE_USER;
    return phys_to_virt(entry & ADDR_MASK);
}

static uint64_t *get_pte(struct address_space *space, uint64_t virt, int create, int user) {
    uint64_t *pml4 = phys_to_virt(space->root);
    size_t i4 = (virt >> 39) & 0x1ff;
    size_t i3 = (virt >> 30) & 0x1ff;
    size_t i2 = (virt >> 21) & 0x1ff;
    size_t i1 = (virt >> 12) & 0x1ff;

    uint64_t *pdpt = next_table(pml4, i4, create, user);
    if (!pdpt) return 0;
    uint64_t *pd = next_table(pdpt, i3, create, user);
    if (!pd) return 0;
    uint64_t *pt = next_table(pd, i2, create, user);
    if (!pt) return 0;
    return &pt[i1];
}

static int vmm_map(struct address_space *space, uint64_t virt, uint64_t phys, uint64_t flags, int user) {
    uint64_t *pte = get_pte(space, virt, 1, user);
    if (!pte) return -1;

    uint64_t bits = PTE_PRESENT;
    if (user) bits |= PTE_USER;
    if (flags & VMM_WRITE) bits |= PTE_WRITE;
    if (flags & VMM_DEVICE) bits |= PTE_PWT | PTE_PCD;
    *pte = (phys & ADDR_MASK) | bits;
    if (read_cr3() == space->root) __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_space_create(struct address_space *space) {
    uint64_t root = pmm_alloc_page();
    if (!root) return -1;
    uint64_t *dst = phys_to_virt(root);
    uint64_t *src = phys_to_virt(read_cr3());
    for (size_t i = 256; i < 512; i++) dst[i] = src[i];
    space->root = root;
    return 0;
}

static int clone_table(uint64_t dst_phys, uint64_t src_phys, unsigned level) {
    uint64_t *dst = phys_to_virt(dst_phys);
    uint64_t *src = phys_to_virt(src_phys);
    for (size_t i = 0; i < 512; i++) {
        uint64_t entry = src[i];
        if (!(entry & PTE_PRESENT)) continue;
        if (level == 1 || (entry & PTE_HUGE)) {
            if ((entry & PTE_USER) && (entry & PTE_WRITE)) {
                entry = (entry & ~PTE_WRITE) | PTE_COW;
                src[i] = entry;
            }
            pmm_page_ref(entry & ADDR_MASK);
            dst[i] = entry;
            continue;
        }
        uint64_t page = pmm_alloc_page();
        if (!page) return -1;
        dst[i] = page | (entry & ~ADDR_MASK);
        if (clone_table(page, entry & ADDR_MASK, level - 1) != 0) return -1;
    }
    return 0;
}

int vmm_space_clone(struct address_space *dst, struct address_space *src) {
    if (vmm_space_create(dst) != 0) return -1;
    uint64_t *to = phys_to_virt(dst->root);
    uint64_t *from = phys_to_virt(src->root);
    int status = 0;
    for (size_t i = 0; i < 256 && !status; i++) {
        if (!(from[i] & PTE_PRESENT)) continue;
        uint64_t page = pmm_alloc_page();
        if (!page) {
            status = -1;
            break;
        }
        to[i] = page | (from[i] & ~ADDR_MASK);
        status = clone_table(page, from[i] & ADDR_MASK, 3);
    }
    if (read_cr3() == src->root) __asm__ volatile ("mov %0, %%cr3" : : "r"(src->root) : "memory");
    if (status) vmm_space_destroy(dst);
    return status;
}

void vmm_space_boot(struct address_space *space) {
    space->root = read_cr3();
}

void vmm_space_activate(struct address_space *space) {
    current_space = space;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(space->root) : "memory");
}

static void free_table(uint64_t phys, unsigned level) {
    uint64_t *table = phys_to_virt(phys);
    for (size_t i = 0; i < 512; i++) {
        uint64_t entry = table[i];
        if (!(entry & PTE_PRESENT)) continue;
        uint64_t child = entry & ADDR_MASK;
        if (level == 1 || (entry & PTE_HUGE)) pmm_free_page(child);
        else free_table(child, level - 1);
    }
    pmm_free_page(phys);
}

void vmm_space_destroy(struct address_space *space) {
    if (!space->root || read_cr3() == space->root) return;
    uint64_t *root = phys_to_virt(space->root);
    for (size_t i = 0; i < 256; i++) {
        if (root[i] & PTE_PRESENT) free_table(root[i] & ADDR_MASK, 3);
    }
    pmm_free_page(space->root);
    space->root = 0;
}

struct address_space *vmm_space_current(void) {
    return current_space;
}

int vmm_map_user(struct address_space *space, uint64_t virt, uint64_t phys, uint64_t flags) {
    return vmm_map(space, virt, phys, flags, 1);
}

int vmm_map_kernel(uint64_t virt, uint64_t phys, uint64_t flags) {
    struct address_space space = { .root = read_cr3() };
    return vmm_map(&space, virt, phys, flags, 0);
}

uint64_t vmm_unmap_kernel(uint64_t virt) {
    struct address_space space = { .root = read_cr3() };
    uint64_t *pte = get_pte(&space, virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return 0;
    uint64_t phys = *pte & ADDR_MASK;
    *pte = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return phys;
}

void vmm_kernel_window(uint64_t *base, uint64_t *size) {
    *base = 0xffffe00000000000ULL;
    *size = 1ULL << 39;
    next_table(phys_to_virt(read_cr3()), (*base >> 39) & 0x1ff, 1, 0);
}

int vmm_protect_user(struct address_space *space, uint64_t virt, uint64_t flags) {
    uint64_t *pte = get_pte(space, virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT) || !(*pte & PTE_USER)) return -1;
    if (flags & VMM_WRITE) *pte |= PTE_WRITE;
    else *pte &= ~PTE_WRITE;
    if (read_cr3() == space->root) __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_unmap_user(struct address_space *space, uint64_t virt) {
    uint64_t *pte = get_pte(space, virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT) || !(*pte & PTE_USER)) return -1;
    uint64_t phys = *pte & ADDR_MASK;
    *pte = 0;
    if (read_cr3() == space->root) __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    pmm_free_page(phys);
    return 0;
}

int vmm_query(struct address_space *space, uint64_t virt, uint64_t *phys, unsigned *flags) {
    uint64_t *pte = get_pte(space, virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return -1;
    *phys = *pte & ADDR_MASK;
    *flags = PAGE_PRESENT | ((*pte & PTE_USER) ? PAGE_USER : 0) |
             ((*pte & PTE_WRITE) ? PAGE_WRITE : 0) | ((*pte & PTE_COW) ? PAGE_COW : 0);
    return 0;
}

int vmm_update(struct address_space *space, uint64_t virt, uint64_t phys, unsigned flags) {
    uint64_t *pte = get_pte(space, virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return -1;
    uint64_t entry = (*pte & ~(ADDR_MASK | PTE_WRITE | PTE_COW)) | (phys & ADDR_MASK);
    if (flags & PAGE_WRITE) entry |= PTE_WRITE;
    if (flags & PAGE_COW) entry |= PTE_COW;
    *pte = entry;
    if (read_cr3() == space->root) __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

uint64_t vmm_user_end(void) {
    return 0x0000800000000000ULL;
}
#endif
