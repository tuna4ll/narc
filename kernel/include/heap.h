#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HEAP_START 0xFFFFC00000000000ull
#define HEAP_MAX   (4ull * 1024 * 1024 * 1024)

struct heap_stats {
    size_t mapped;
    size_t used;
    size_t free;
    size_t blocks;
    size_t free_blocks;
};

bool heap_init(void);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

void heap_get_stats(struct heap_stats *st);
bool heap_check(void);

#endif
