#include <kernel/mm.h>

#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)
#define SATP_MODE_MASK (0xfULL << 60)
#define SATP_PPN_MASK  ((1ULL << 44) - 1)

static struct address_space *current_space;

static uint64_t read_satp(void) {
    uint64_t value;
    __asm__ volatile ("csrr %0, satp" : "=r"(value));
    return value;
}

static unsigned levels(void) {
    return (read_satp() >> 60) == 9 ? 4 : 3;
}

static uint64_t pte_phys(uint64_t pte) {
    return (pte >> 10) << 12;
}

static uint64_t make_pte(uint64_t phys, uint64_t flags) {
    return (phys >> 12 << 10) | flags;
}

static uint64_t *next_table(uint64_t *table, size_t index, int create) {
    uint64_t pte = table[index];
    if (!(pte & PTE_V)) {
        if (!create) return 0;
        uint64_t phys = pmm_alloc_page();
        if (!phys) return 0;
        table[index] = make_pte(phys, PTE_V);
        return phys_to_virt(phys);
    }
    if (pte & (PTE_R | PTE_W | PTE_X)) return 0;
    return phys_to_virt(pte_phys(pte));
}

static uint64_t *get_pte(uint64_t root, uint64_t virt, int create) {
    uint64_t *table = phys_to_virt(root);
    for (int level = (int)levels() - 1; level > 0; level--) {
        table = next_table(table, (virt >> (12 + level * 9)) & 0x1ff, create);
        if (!table) return 0;
    }
    return &table[(virt >> 12) & 0x1ff];
}

static void flush_tlb(void) {
    __asm__ volatile ("sfence.vma" ::: "memory");
}

static int map_page(uint64_t root, uint64_t virt, uint64_t phys, uint64_t flags, int user) {
    uint64_t *pte = get_pte(root, virt, 1);
    if (!pte) return -1;
    uint64_t bits = PTE_V | PTE_R | PTE_A | PTE_D;
    if (flags & VMM_WRITE) bits |= PTE_W;
    if (user) bits |= PTE_U | PTE_X;
    *pte = make_pte(phys, bits);
    flush_tlb();
    return 0;
}

int vmm_space_create(struct address_space *space) {
    uint64_t root = pmm_alloc_page();
    if (!root) return -1;
    uint64_t current_root = (read_satp() & SATP_PPN_MASK) << 12;
    uint64_t *dst = phys_to_virt(root);
    uint64_t *src = phys_to_virt(current_root);
    for (size_t i = 256; i < 512; i++) dst[i] = src[i];
    space->root = root;
    return 0;
}

static int clone_table(uint64_t dst_phys, uint64_t src_phys, unsigned level) {
    uint64_t *dst = phys_to_virt(dst_phys);
    uint64_t *src = phys_to_virt(src_phys);
    for (size_t i = 0; i < 512; i++) {
        uint64_t pte = src[i];
        if (!(pte & PTE_V)) continue;
        uint64_t page = pmm_alloc_page();
        if (!page) return -1;
        dst[i] = make_pte(page, pte & 0x3ff);
        if (pte & (PTE_R | PTE_W | PTE_X))
            __builtin_memcpy(phys_to_virt(page), phys_to_virt(pte_phys(pte)), PAGE_SIZE);
        else if (!level || clone_table(page, pte_phys(pte), level - 1) != 0)
            return -1;
    }
    return 0;
}

int vmm_space_clone(struct address_space *dst, struct address_space *src) {
    if (vmm_space_create(dst) != 0) return -1;
    uint64_t *to = phys_to_virt(dst->root);
    uint64_t *from = phys_to_virt(src->root);
    for (size_t i = 0; i < 256; i++) {
        if (!(from[i] & PTE_V)) continue;
        uint64_t page = pmm_alloc_page();
        if (!page) goto fail;
        to[i] = make_pte(page, from[i] & 0x3ff);
        if (clone_table(page, pte_phys(from[i]), levels() - 2) != 0) goto fail;
    }
    return 0;
fail:
    vmm_space_destroy(dst);
    return -1;
}

void vmm_space_activate(struct address_space *space) {
    current_space = space;
    uint64_t satp = (read_satp() & SATP_MODE_MASK) | (space->root >> 12);
    __asm__ volatile ("csrw satp, %0; sfence.vma" : : "r"(satp) : "memory");
}

static void free_table(uint64_t phys, unsigned level) {
    uint64_t *table = phys_to_virt(phys);
    for (size_t i = 0; i < 512; i++) {
        uint64_t pte = table[i];
        if (!(pte & PTE_V)) continue;
        uint64_t child = pte_phys(pte);
        if (level == 1 || (pte & (PTE_R | PTE_W | PTE_X))) pmm_free_page(child);
        else free_table(child, level - 1);
    }
    pmm_free_page(phys);
}

void vmm_space_destroy(struct address_space *space) {
    uint64_t active = (read_satp() & SATP_PPN_MASK) << 12;
    if (!space->root || active == space->root) return;
    uint64_t *root = phys_to_virt(space->root);
    for (size_t i = 0; i < 256; i++) {
        if (root[i] & PTE_V) free_table(pte_phys(root[i]), levels() - 1);
    }
    pmm_free_page(space->root);
    space->root = 0;
}

struct address_space *vmm_space_current(void) {
    return current_space;
}

int vmm_map_user(struct address_space *space, uint64_t virt, uint64_t phys, uint64_t flags) {
    return map_page(space->root, virt, phys, flags, 1);
}

int vmm_map_kernel(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t root = (read_satp() & SATP_PPN_MASK) << 12;
    return map_page(root, virt, phys, flags, 0);
}

uint64_t vmm_unmap_kernel(uint64_t virt) {
    uint64_t *pte = get_pte((read_satp() & SATP_PPN_MASK) << 12, virt, 0);
    if (!pte || !(*pte & PTE_V)) return 0;
    uint64_t phys = pte_phys(*pte);
    *pte = 0;
    flush_tlb();
    return phys;
}

void vmm_kernel_window(uint64_t *base, uint64_t *size) {
    unsigned shift = levels() == 4 ? 39 : 30;
    *base = levels() == 4 ? 0xffffe00000000000ULL : 0xfffffff800000000ULL;
    *size = 1ULL << shift;
    uint64_t *root = phys_to_virt((read_satp() & SATP_PPN_MASK) << 12);
    next_table(root, (*base >> shift) & 0x1ff, 1);
}

int vmm_protect_user(struct address_space *space, uint64_t virt, uint64_t flags) {
    uint64_t *pte = get_pte(space->root, virt, 0);
    if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return -1;
    if (flags & VMM_WRITE) *pte |= PTE_W;
    else *pte &= ~PTE_W;
    flush_tlb();
    return 0;
}

int vmm_unmap_user(struct address_space *space, uint64_t virt) {
    uint64_t *pte = get_pte(space->root, virt, 0);
    if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return -1;
    uint64_t phys = pte_phys(*pte);
    *pte = 0;
    flush_tlb();
    pmm_free_page(phys);
    return 0;
}

uint64_t vmm_user_phys(struct address_space *space, uint64_t virt) {
    uint64_t *pte = get_pte(space->root, virt, 0);
    if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return 0;
    return pte_phys(*pte) | (virt & (PAGE_SIZE - 1));
}

uint64_t vmm_user_end(void) {
    return levels() == 4 ? 0x0000800000000000ULL : 0x0000004000000000ULL;
}

int vmm_user_range_ok(struct address_space *space, uint64_t virt, uint64_t len, int write) {
    if (!len) return 1;
    if (virt >= vmm_user_end() || len > vmm_user_end() - virt) return 0;
    uint64_t last = (virt + len - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t page = virt & ~(PAGE_SIZE - 1);; page += PAGE_SIZE) {
        uint64_t *pte = get_pte(space->root, page, 0);
        if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return 0;
        if (write && !(*pte & PTE_W)) return 0;
        if (page == last) break;
    }
    return 1;
}
