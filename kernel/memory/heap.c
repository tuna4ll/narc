#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "kprint.h"

#define BLOCK_MAGIC 0x4E415243484541ull
#define ALIGN 16
#define MIN_SPLIT 32
#define GROW_MIN (64 * 1024)
#define TRIM_THRESHOLD (256 * 1024)

struct block {
    uint64_t magic;
    size_t size;
    bool free;
    struct block *prev_phys;
    struct block *next_free;
    struct block *prev_free;
};

#define HDR sizeof(struct block)

_Static_assert(sizeof(struct block) % ALIGN == 0, "block header must keep alignment");

static uint64_t heap_end = HEAP_START;
static struct block *free_list = NULL;
static struct block *last_block = NULL;

static uint64_t heap_lock(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void heap_unlock(uint64_t flags) {
    if (flags & (1 << 9))
        __asm__ volatile ("sti" : : : "memory");
}

static void heap_warn(const char *msg, void *ptr) {
    kprint("HEAP: %s %p\n", msg, ptr);
}

static size_t align_up(size_t n) {
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

static struct block *next_phys(struct block *b) {
    uint64_t n = (uint64_t)b + HDR + b->size;
    if (n >= heap_end)
        return NULL;
    return (struct block *)n;
}

static struct block *ptr_to_block(void *ptr) {
    uint64_t a = (uint64_t)ptr;

    if (a < HEAP_START + HDR || a >= heap_end || (a & (ALIGN - 1)))
        return NULL;

    struct block *b = (struct block *)(a - HDR);
    if (b->magic != BLOCK_MAGIC)
        return NULL;
    return b;
}

static void free_list_add(struct block *b) {
    b->prev_free = NULL;
    b->next_free = free_list;
    if (free_list)
        free_list->prev_free = b;
    free_list = b;
}

static void free_list_remove(struct block *b) {
    if (b->prev_free)
        b->prev_free->next_free = b->next_free;
    else
        free_list = b->next_free;
    if (b->next_free)
        b->next_free->prev_free = b->prev_free;
    b->next_free = b->prev_free = NULL;
}

static struct block *coalesce(struct block *b) {
    struct block *n = next_phys(b);
    if (n && n->free) {
        free_list_remove(n);
        b->size += HDR + n->size;
    }

    struct block *pr = b->prev_phys;
    if (pr && pr->free) {
        free_list_remove(pr);
        pr->size += HDR + b->size;
        b = pr;
    }

    n = next_phys(b);
    if (n)
        n->prev_phys = b;
    else
        last_block = b;

    return b;
}

static bool heap_grow(size_t bytes) {
    bytes = (bytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);

    if (bytes > HEAP_MAX - (heap_end - HEAP_START))
        return false;

    for (uint64_t v = heap_end; v < heap_end + bytes; v += PAGE_SIZE) {
        uint64_t p = pmm_alloc_page();
        if (p == 0)
            return false;
        if (!vmm_map_page(v, p, PTE_WRITE | PTE_NX)) {
            pmm_free_page(p);
            return false;
        }
    }

    struct block *b = (struct block *)heap_end;
    b->magic = BLOCK_MAGIC;
    b->size = bytes - HDR;
    b->free = true;
    b->prev_phys = last_block;

    last_block = b;
    heap_end += bytes;

    free_list_add(coalesce(b));
    return true;
}

static void heap_trim(void) {
    struct block *b = last_block;
    if (b == NULL || !b->free)
        return;

    uint64_t data = (uint64_t)b + HDR;
    uint64_t keep = (data + MIN_SPLIT + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    if (keep < HEAP_START + GROW_MIN)
        keep = HEAP_START + GROW_MIN;

    if (keep >= heap_end || heap_end - keep < TRIM_THRESHOLD)
        return;

    for (uint64_t v = keep; v < heap_end; v += PAGE_SIZE)
        pmm_free_page(vmm_unmap_page(v));

    b->size = keep - data;
    heap_end = keep;
}

static void split(struct block *b, size_t size) {
    if (b->size < size + HDR + MIN_SPLIT)
        return;

    struct block *rest = (struct block *)((uint8_t *)b + HDR + size);
    rest->magic = BLOCK_MAGIC;
    rest->size = b->size - size - HDR;
    rest->free = true;
    rest->prev_phys = b;

    b->size = size;
    free_list_add(coalesce(rest));
}

bool heap_init(void) {
    return heap_grow(GROW_MIN);
}

static void *alloc_locked(size_t size) {
    for (;;) {
        for (struct block *b = free_list; b; b = b->next_free) {
            if (b->size >= size) {
                free_list_remove(b);
                b->free = false;
                split(b, size);
                return (uint8_t *)b + HDR;
            }
        }

        size_t grow = size + HDR;
        if (grow < GROW_MIN)
            grow = GROW_MIN;
        if (!heap_grow(grow))
            return NULL;
    }
}

static bool free_locked(void *ptr) {
    struct block *b = ptr_to_block(ptr);
    if (b == NULL) {
        heap_warn("kfree: invalid pointer", ptr);
        return false;
    }
    if (b->free) {
        heap_warn("kfree: double free", ptr);
        return false;
    }

    b->free = true;
    free_list_add(coalesce(b));
    heap_trim();
    return true;
}

void *kmalloc(size_t size) {
    if (size == 0 || size > HEAP_MAX)
        return NULL;

    uint64_t f = heap_lock();
    void *p = alloc_locked(align_up(size));
    heap_unlock(f);
    return p;
}

void kfree(void *ptr) {
    if (ptr == NULL)
        return;

    uint64_t f = heap_lock();
    free_locked(ptr);
    heap_unlock(f);
}

void *kcalloc(size_t count, size_t size) {
    if (size != 0 && count > HEAP_MAX / size)
        return NULL;

    void *p = kmalloc(count * size);
    if (p)
        memset(p, 0, count * size);
    return p;
}

void *krealloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    if (size > HEAP_MAX)
        return NULL;

    size = align_up(size);

    uint64_t f = heap_lock();

    struct block *b = ptr_to_block(ptr);
    if (b == NULL || b->free) {
        heap_warn("krealloc: invalid pointer", ptr);
        heap_unlock(f);
        return NULL;
    }

    if (b->size >= size) {
        split(b, size);
        heap_unlock(f);
        return ptr;
    }

    struct block *n = next_phys(b);
    if (n && n->free && b->size + HDR + n->size >= size) {
        free_list_remove(n);
        b->size += HDR + n->size;

        n = next_phys(b);
        if (n)
            n->prev_phys = b;
        else
            last_block = b;

        split(b, size);
        heap_unlock(f);
        return ptr;
    }

    void *np = alloc_locked(size);
    if (np) {
        memcpy(np, ptr, b->size);
        free_locked(ptr);
    }

    heap_unlock(f);
    return np;
}

void heap_get_stats(struct heap_stats *st) {
    uint64_t f = heap_lock();

    memset(st, 0, sizeof(*st));
    st->mapped = heap_end - HEAP_START;

    for (struct block *b = (struct block *)HEAP_START; b; b = next_phys(b)) {
        st->blocks++;
        if (b->free) {
            st->free += b->size;
            st->free_blocks++;
        } else {
            st->used += b->size;
        }
    }

    heap_unlock(f);
}

bool heap_check(void) {
    uint64_t f = heap_lock();
    bool ok = true;
    size_t free_count = 0;
    struct block *prev = NULL;

    for (struct block *b = (struct block *)HEAP_START; b; b = next_phys(b)) {
        if (b->magic != BLOCK_MAGIC || b->prev_phys != prev ||
            (b->size & (ALIGN - 1)) ||
            (b->free && prev && prev->free)) {
            heap_warn("heap_check: corrupt block at", b);
            ok = false;
            break;
        }
        if (b->free)
            free_count++;
        prev = b;
    }

    if (ok && prev != last_block) {
        heap_warn("heap_check: last_block mismatch", last_block);
        ok = false;
    }

    size_t listed = 0;
    for (struct block *b = free_list; ok && b; b = b->next_free) {
        if (!b->free || b->magic != BLOCK_MAGIC) {
            heap_warn("heap_check: bad free list entry", b);
            ok = false;
        }
        listed++;
    }

    if (ok && listed != free_count) {
        heap_warn("heap_check: free list count mismatch", (void *)listed);
        ok = false;
    }

    heap_unlock(f);
    return ok;
}
