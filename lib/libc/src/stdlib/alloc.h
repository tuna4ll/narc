#pragma once

#include <stddef.h>

typedef union allocation_block allocation_block_t;
typedef struct allocation_arena allocation_arena_t;

union allocation_block {
    struct {
        size_t size;
        allocation_block_t *previous;
        allocation_block_t *next;
        allocation_arena_t *arena;
        int free;
    } value;
    max_align_t alignment;
};

struct allocation_arena {
    size_t mapped;
    size_t active;
    allocation_arena_t *previous;
    allocation_arena_t *next;
};

void *__allocator_allocate(size_t size);
void __allocator_release(void *pointer);
size_t __allocator_size(const void *pointer);
int __allocator_resize(void *pointer, size_t size);
