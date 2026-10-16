#include <caposh/memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHUNK_SIZE 8192
#define ALIGNMENT 16

struct arena_chunk {
    struct arena_chunk *previous;
    size_t size, used;
    _Alignas(ALIGNMENT) unsigned char data[];
};

static struct arena stack_arena;
struct arena *stack = &stack_arena;

static _Noreturn void out_of_memory(void) {
    static const char message[] = "caposh: out of memory\n";
    write(2, message, sizeof(message) - 1);
    _exit(2);
}

void *xmalloc(size_t size) {
    void *pointer = malloc(size ? size : 1);
    if (!pointer) out_of_memory();
    return pointer;
}

void *xcalloc(size_t count, size_t size) {
    void *pointer = calloc(count ? count : 1, size ? size : 1);
    if (!pointer) out_of_memory();
    return pointer;
}

void *xrealloc(void *pointer, size_t size) {
    void *next = realloc(pointer, size ? size : 1);
    if (!next) out_of_memory();
    return next;
}

char *xstrdup(const char *string) {
    return xstrndup(string, strlen(string));
}

char *xstrndup(const char *string, size_t length) {
    char *copy = xmalloc(length + 1);
    memcpy(copy, string, length);
    copy[length] = 0;
    return copy;
}

void arena_init(struct arena *arena) {
    arena->current = 0;
}

static size_t align(size_t size) {
    return (size + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1);
}

void *arena_alloc(struct arena *arena, size_t size) {
    size = align(size ? size : 1);
    struct arena_chunk *chunk = arena->current;
    if (!chunk || chunk->size - chunk->used < size) {
        size_t capacity = size > CHUNK_SIZE ? size : CHUNK_SIZE;
        if (capacity > SIZE_MAX - sizeof(*chunk)) out_of_memory();
        chunk = xmalloc(sizeof(*chunk) + capacity);
        chunk->previous = arena->current;
        chunk->size = capacity;
        chunk->used = 0;
        arena->current = chunk;
    }
    void *pointer = chunk->data + chunk->used;
    chunk->used += size;
    return pointer;
}

char *arena_strndup(struct arena *arena, const char *string, size_t length) {
    char *copy = arena_alloc(arena, length + 1);
    memcpy(copy, string, length);
    copy[length] = 0;
    return copy;
}

char *arena_strdup(struct arena *arena, const char *string) {
    return arena_strndup(arena, string, strlen(string));
}

struct arena_mark arena_mark(struct arena *arena) {
    return (struct arena_mark) { arena->current, arena->current ? arena->current->used : 0 };
}

void arena_release(struct arena *arena, struct arena_mark mark) {
    while (arena->current && arena->current != mark.chunk) {
        struct arena_chunk *previous = arena->current->previous;
        free(arena->current);
        arena->current = previous;
    }
    if (arena->current) arena->current->used = mark.used;
}

void arena_destroy(struct arena *arena) {
    arena_release(arena, (struct arena_mark) { 0, 0 });
}

void *stalloc(size_t size) {
    return arena_alloc(stack, size);
}

char *ststrdup(const char *string) {
    return arena_strdup(stack, string);
}

char *ststrndup(const char *string, size_t length) {
    return arena_strndup(stack, string, length);
}
