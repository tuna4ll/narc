#include <caposh/strbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void strbuf_init(struct strbuf *buffer, struct arena *arena) {
    buffer->arena = arena;
    buffer->data = 0;
    buffer->length = buffer->capacity = 0;
}

void strbuf_reserve(struct strbuf *buffer, size_t extra) {
    size_t needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity) return;
    size_t capacity = buffer->capacity ? buffer->capacity : 32;
    while (capacity < needed) capacity *= 2;
    if (buffer->arena) {
        char *data = arena_alloc(buffer->arena, capacity);
        if (buffer->length) memcpy(data, buffer->data, buffer->length);
        buffer->data = data;
    } else {
        buffer->data = xrealloc(buffer->data, capacity);
    }
    buffer->capacity = capacity;
}

void strbuf_putc(struct strbuf *buffer, char c) {
    strbuf_reserve(buffer, 1);
    buffer->data[buffer->length++] = c;
    buffer->data[buffer->length] = 0;
}

void strbuf_append(struct strbuf *buffer, const char *data, size_t length) {
    strbuf_reserve(buffer, length);
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = 0;
}

void strbuf_puts(struct strbuf *buffer, const char *string) {
    strbuf_append(buffer, string, strlen(string));
}

void strbuf_vprintf(struct strbuf *buffer, const char *format, va_list arguments) {
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(0, 0, format, copy);
    va_end(copy);
    if (length <= 0) return;
    strbuf_reserve(buffer, (size_t)length);
    vsnprintf(buffer->data + buffer->length, (size_t)length + 1, format, arguments);
    buffer->length += (size_t)length;
}

void strbuf_printf(struct strbuf *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    strbuf_vprintf(buffer, format, arguments);
    va_end(arguments);
}

void strbuf_truncate(struct strbuf *buffer, size_t length) {
    if (length >= buffer->length) return;
    buffer->length = length;
    buffer->data[length] = 0;
}

char *strbuf_string(struct strbuf *buffer) {
    strbuf_reserve(buffer, 0);
    buffer->data[buffer->length] = 0;
    return buffer->data;
}

void strbuf_free(struct strbuf *buffer) {
    if (!buffer->arena) free(buffer->data);
    strbuf_init(buffer, buffer->arena);
}
