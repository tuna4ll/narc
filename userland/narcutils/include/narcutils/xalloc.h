#pragma once

#include <stddef.h>
#include <stdnoreturn.h>

noreturn void xalloc_die(void);
void *xmalloc(size_t size);
void *xzalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *pointer, size_t size);
void *xnmalloc(size_t count, size_t size);
void *xnrealloc(void *pointer, size_t count, size_t size);
void *x2nrealloc(void *pointer, size_t *count, size_t size);
char *xstrdup(const char *string);
char *xstrndup(const char *string, size_t length);
void *xmemdup(const void *source, size_t size);
char *xasprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
char *file_name_concat(const char *directory, const char *name);
