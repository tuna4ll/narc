#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <stdint.h>

#define HEAP_MAGIC 0x6e617263u
#define HEAP_HEADER 16
#define CLASS_COUNT 7

struct page_header {
    uint32_t magic;
    uint32_t class;
    uint64_t pages;
};

struct free_object {
    struct free_object *next;
};

struct range {
    uint64_t base, pages;
    struct range *next;
};

static const size_t class_size[CLASS_COUNT] = { 16, 32, 64, 128, 256, 512, 1024 };
static struct free_object *free_objects[CLASS_COUNT];
static struct range *free_ranges;
static uint64_t window_base, window_end, window_next;

void heap_init(void) {
    uint64_t size;
    vmm_kernel_window(&window_base, &size);
    window_end = window_base + size;
    window_next = window_base;
}

static int class_for(size_t size) {
    for (int i = 0; i < CLASS_COUNT; i++)
        if (size <= class_size[i]) return i;
    return -1;
}

static int refill(int class) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return -1;
    uint8_t *page = phys_to_virt(phys);
    struct page_header *header = (struct page_header *)page;
    header->magic = HEAP_MAGIC;
    header->class = (uint32_t)class;
    header->pages = 0;
    size_t size = class_size[class];
    for (size_t offset = HEAP_HEADER; offset + size <= PAGE_SIZE; offset += size) {
        struct free_object *object = (struct free_object *)(page + offset);
        object->next = free_objects[class];
        free_objects[class] = object;
    }
    return 0;
}

static void *small_alloc(int class) {
    if (!free_objects[class] && refill(class) != 0) return 0;
    struct free_object *object = free_objects[class];
    free_objects[class] = object->next;
    return object;
}

static uint64_t window_alloc(uint64_t pages) {
    for (struct range **link = &free_ranges; *link; link = &(*link)->next) {
        struct range *range = *link;
        if (range->pages < pages) continue;
        uint64_t base = range->base;
        range->base += pages * PAGE_SIZE;
        range->pages -= pages;
        if (!range->pages) {
            *link = range->next;
            kfree(range);
        }
        return base;
    }
    if (pages > (window_end - window_next) / PAGE_SIZE) return 0;
    uint64_t base = window_next;
    window_next += pages * PAGE_SIZE;
    return base;
}

static void window_free(uint64_t base, uint64_t pages) {
    struct range *prev = 0, *next = free_ranges;
    while (next && next->base < base) {
        prev = next;
        next = next->next;
    }
    if (prev && prev->base + prev->pages * PAGE_SIZE == base) {
        prev->pages += pages;
        if (next && prev->base + prev->pages * PAGE_SIZE == next->base) {
            prev->pages += next->pages;
            prev->next = next->next;
            kfree(next);
        }
        return;
    }
    if (next && base + pages * PAGE_SIZE == next->base) {
        next->base = base;
        next->pages += pages;
        return;
    }
    struct range *range = kmalloc(sizeof(*range));
    if (!range) return;
    range->base = base;
    range->pages = pages;
    range->next = next;
    if (prev) prev->next = range;
    else free_ranges = range;
}

static void unmap_pages(uint64_t base, uint64_t pages) {
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = vmm_unmap_kernel(base + i * PAGE_SIZE);
        if (phys) pmm_free_page(phys);
    }
}

static void *large_alloc(size_t size) {
    if (size > SIZE_MAX - HEAP_HEADER - PAGE_SIZE) return 0;
    uint64_t pages = (size + HEAP_HEADER + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t base = window_alloc(pages);
    if (!base) return 0;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys || vmm_map_kernel(base + i * PAGE_SIZE, phys, VMM_WRITE) != 0) {
            if (phys) pmm_free_page(phys);
            unmap_pages(base, i);
            window_free(base, pages);
            return 0;
        }
    }
    struct page_header *header = (struct page_header *)(uintptr_t)base;
    header->magic = HEAP_MAGIC;
    header->class = CLASS_COUNT;
    header->pages = pages;
    return (uint8_t *)header + HEAP_HEADER;
}

static struct page_header *header_of(void *ptr) {
    struct page_header *header = (struct page_header *)((uintptr_t)ptr & ~(PAGE_SIZE - 1));
    return header->magic == HEAP_MAGIC ? header : 0;
}

static size_t usable_size(struct page_header *header) {
    if (header->class < CLASS_COUNT) return class_size[header->class];
    return header->pages * PAGE_SIZE - HEAP_HEADER;
}

void *kmalloc(size_t size) {
    if (!size) size = 1;
    int class = class_for(size);
    return class >= 0 ? small_alloc(class) : large_alloc(size);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    struct page_header *header = header_of(ptr);
    if (!header) return 0;
    size_t old = usable_size(header);
    if (size <= old) return ptr;
    void *next = kmalloc(size);
    if (!next) return 0;
    memcpy(next, ptr, old);
    kfree(ptr);
    return next;
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct page_header *header = header_of(ptr);
    if (!header) return;
    if (header->class < CLASS_COUNT) {
        struct free_object *object = ptr;
        object->next = free_objects[header->class];
        free_objects[header->class] = object;
        return;
    }
    uint64_t base = (uint64_t)(uintptr_t)header, pages = header->pages;
    header->magic = 0;
    unmap_pages(base, pages);
    window_free(base, pages);
}
