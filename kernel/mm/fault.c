#include <kernel/mm.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>

static struct spinlock fault_lock = SPINLOCK_INIT;

static int stack_growth(uint64_t page) {
    return page >= USER_STACK_TOP - USER_STACK_LIMIT && page < USER_STACK_TOP;
}

static int break_copy(struct address_space *space, uint64_t page, uint64_t phys) {
    if (pmm_page_refs(phys) == 1) return vmm_update(space, page, phys, PAGE_WRITE);
    uint64_t copy = pmm_alloc_page();
    if (!copy) return -1;
    memcpy(phys_to_virt(copy), phys_to_virt(phys), PAGE_SIZE);
    if (vmm_update(space, page, copy, PAGE_WRITE) != 0) {
        pmm_free_page(copy);
        return -1;
    }
    pmm_free_page(phys);
    return 0;
}

static int resolve_locked(struct address_space *space, uint64_t page, int write) {
    uint64_t phys;
    unsigned flags;
    if (vmm_query(space, page, &phys, &flags) == 0) {
        if (!(flags & PAGE_USER)) return -1;
        if (!write || (flags & PAGE_WRITE)) return 0;
        if (!(flags & PAGE_COW)) return -1;
        return break_copy(space, page, phys);
    }
    if (!stack_growth(page)) return -1;
    uint64_t fresh = pmm_alloc_page();
    if (!fresh) return -1;
    if (vmm_map_user(space, page, fresh, VMM_WRITE) != 0) {
        pmm_free_page(fresh);
        return -1;
    }
    return 0;
}

int vmm_resolve(struct address_space *space, uint64_t virt, int write) {
    if (virt >= vmm_user_end()) return -1;
    unsigned long flags = spin_lock_irqsave(&fault_lock);
    int status = resolve_locked(space, virt & ~(PAGE_SIZE - 1), write);
    spin_unlock_irqrestore(&fault_lock, flags);
    return status;
}

uint64_t vmm_user_page(struct address_space *space, uint64_t virt, int write) {
    if (vmm_resolve(space, virt, write) != 0) return 0;
    return vmm_user_phys(space, virt);
}

uint64_t vmm_user_phys(struct address_space *space, uint64_t virt) {
    uint64_t phys;
    unsigned flags;
    if (vmm_query(space, virt & ~(PAGE_SIZE - 1), &phys, &flags) != 0 || !(flags & PAGE_USER))
        return 0;
    return phys | (virt & (PAGE_SIZE - 1));
}

int vmm_user_range_ok(struct address_space *space, uint64_t virt, uint64_t len, int write) {
    if (!len) return 1;
    if (virt >= vmm_user_end() || len > vmm_user_end() - virt) return 0;
    uint64_t last = (virt + len - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t page = virt & ~(PAGE_SIZE - 1);; page += PAGE_SIZE) {
        uint64_t phys;
        unsigned flags;
        if (vmm_query(space, page, &phys, &flags) == 0) {
            if (!(flags & PAGE_USER)) return 0;
            if (write && !(flags & (PAGE_WRITE | PAGE_COW))) return 0;
        } else if (!stack_growth(page)) {
            return 0;
        }
        if (page == last) break;
    }
    return 1;
}
