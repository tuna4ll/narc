#include "alloc.h"
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096u
#define ARENA_SIZE (64u * 1024u)

static allocation_arena_t *arenas;

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static size_t arena_header_size(void) {
    return align_up(sizeof(allocation_arena_t), _Alignof(max_align_t));
}

static allocation_block_t *first_block(allocation_arena_t *arena) {
    return (allocation_block_t *)((unsigned char *)arena + arena_header_size());
}

static void split_block(allocation_block_t *block, size_t size) {
    size_t remaining = block->value.size - size;
    if (remaining < sizeof(allocation_block_t) + _Alignof(max_align_t)) return;
    allocation_block_t *split = (allocation_block_t *)
        ((unsigned char *)(block + 1) + size);
    split->value.size = remaining - sizeof(allocation_block_t);
    split->value.previous = block;
    split->value.next = block->value.next;
    split->value.arena = block->value.arena;
    split->value.free = 1;
    if (split->value.next) split->value.next->value.previous = split;
    if (split->value.next && split->value.next->value.free) {
        split->value.size += sizeof(allocation_block_t) + split->value.next->value.size;
        split->value.next = split->value.next->value.next;
        if (split->value.next) split->value.next->value.previous = split;
    }
    block->value.next = split;
    block->value.size = size;
}

static void merge_next(allocation_block_t *block) {
    allocation_block_t *next = block->value.next;
    if (!next || !next->value.free) return;
    block->value.size += sizeof(allocation_block_t) + next->value.size;
    block->value.next = next->value.next;
    if (block->value.next) block->value.next->value.previous = block;
}

static allocation_arena_t *create_arena(size_t size) {
    size_t overhead = arena_header_size() + sizeof(allocation_block_t);
    if (size > SIZE_MAX - overhead - (PAGE_SIZE - 1)) return 0;
    size_t required = align_up(size + overhead, PAGE_SIZE);
    size_t mapped = required > ARENA_SIZE ? required : ARENA_SIZE;
    allocation_arena_t *arena = mmap(0, mapped, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) return 0;
    arena->mapped = mapped;
    arena->active = 0;
    arena->previous = 0;
    arena->next = arenas;
    if (arenas) arenas->previous = arena;
    arenas = arena;

    allocation_block_t *block = first_block(arena);
    block->value.size = mapped - overhead;
    block->value.previous = 0;
    block->value.next = 0;
    block->value.arena = arena;
    block->value.free = 1;
    return arena;
}

static allocation_block_t *find_block(size_t size) {
    for (allocation_arena_t *arena = arenas; arena; arena = arena->next)
        for (allocation_block_t *block = first_block(arena); block;
             block = block->value.next)
            if (block->value.free && block->value.size >= size) return block;
    return 0;
}

void *__allocator_allocate(size_t size) {
    size_t alignment = _Alignof(max_align_t);
    if (!size) size = 1;
    if (size > SIZE_MAX - (alignment - 1)) {
        errno = ENOMEM;
        return 0;
    }
    size = align_up(size, alignment);

    allocation_block_t *block = find_block(size);
    if (!block) {
        allocation_arena_t *arena = create_arena(size);
        if (!arena) {
            errno = ENOMEM;
            return 0;
        }
        block = first_block(arena);
    }
    split_block(block, size);
    block->value.free = 0;
    block->value.arena->active++;
    return block + 1;
}

void __allocator_release(void *pointer) {
    if (!pointer) return;
    allocation_block_t *block = (allocation_block_t *)pointer - 1;
    allocation_arena_t *arena = block->value.arena;
    block->value.free = 1;
    if (arena->active) arena->active--;
    merge_next(block);
    if (block->value.previous && block->value.previous->value.free) {
        block = block->value.previous;
        merge_next(block);
    }
    if (arena->active || (!arena->previous && !arena->next)) return;
    if (arena->previous) arena->previous->next = arena->next;
    else arenas = arena->next;
    if (arena->next) arena->next->previous = arena->previous;
    size_t mapped = arena->mapped;
    munmap(arena, mapped);
}

size_t __allocator_size(const void *pointer) {
    const allocation_block_t *block = (const allocation_block_t *)pointer - 1;
    return block->value.size;
}

int __allocator_resize(void *pointer, size_t size) {
    size_t alignment = _Alignof(max_align_t);
    if (size > SIZE_MAX - (alignment - 1)) return -1;
    size = align_up(size, alignment);
    allocation_block_t *block = (allocation_block_t *)pointer - 1;
    if (block->value.size >= size) {
        split_block(block, size);
        return 0;
    }
    allocation_block_t *next = block->value.next;
    if (!next || !next->value.free ||
        block->value.size + sizeof(allocation_block_t) + next->value.size < size)
        return -1;
    merge_next(block);
    split_block(block, size);
    return 0;
}
