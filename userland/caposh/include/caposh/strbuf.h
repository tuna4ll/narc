#pragma once

#include <caposh/memory.h>
#include <stdarg.h>
#include <stddef.h>

struct strbuf {
    struct arena *arena;
    char *data;
    size_t length, capacity;
};

void strbuf_init(struct strbuf *buffer, struct arena *arena);
void strbuf_reserve(struct strbuf *buffer, size_t extra);
void strbuf_putc(struct strbuf *buffer, char c);
void strbuf_append(struct strbuf *buffer, const char *data, size_t length);
void strbuf_puts(struct strbuf *buffer, const char *string);
void strbuf_printf(struct strbuf *buffer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void strbuf_vprintf(struct strbuf *buffer, const char *format, va_list arguments);
void strbuf_truncate(struct strbuf *buffer, size_t length);
char *strbuf_string(struct strbuf *buffer);
void strbuf_free(struct strbuf *buffer);
