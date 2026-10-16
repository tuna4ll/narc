#pragma once

#include <stddef.h>

struct arena_chunk;

struct arena {
    struct arena_chunk *current;
};

struct arena_mark {
    struct arena_chunk *chunk;
    size_t used;
};

void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *pointer, size_t size);
char *xstrdup(const char *string);
char *xstrndup(const char *string, size_t length);

void arena_init(struct arena *arena);
void *arena_alloc(struct arena *arena, size_t size);
char *arena_strdup(struct arena *arena, const char *string);
char *arena_strndup(struct arena *arena, const char *string, size_t length);
struct arena_mark arena_mark(struct arena *arena);
void arena_release(struct arena *arena, struct arena_mark mark);
void arena_destroy(struct arena *arena);

extern struct arena *stack;

void *stalloc(size_t size);
char *ststrdup(const char *string);
char *ststrndup(const char *string, size_t length);
