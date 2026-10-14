#include "internal.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct buffer_sink {
    char *buffer;
    size_t size, used;
};

static void put_buffer(struct format_sink *sink, const char *data, size_t length) {
    struct buffer_sink *out = sink->context;
    if (out->used + 1 < out->size) {
        size_t room = out->size - 1 - out->used;
        size_t chunk = length < room ? length : room;
        memcpy(out->buffer + out->used, data, chunk);
    }
    out->used += length;
}

int vsnprintf(char *restrict buffer, size_t size, const char *restrict format,
              va_list arguments) {
    struct buffer_sink out = { buffer, size, 0 };
    struct format_sink sink = { put_buffer, &out, 0, 0 };
    int result = __stdio_format(&sink, format, arguments);
    if (size) buffer[out.used < size ? out.used : size - 1] = 0;
    return result;
}

int snprintf(char *restrict buffer, size_t size, const char *restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

int vsprintf(char *restrict buffer, const char *restrict format, va_list arguments) {
    return vsnprintf(buffer, SIZE_MAX, format, arguments);
}

int sprintf(char *restrict buffer, const char *restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, SIZE_MAX, format, arguments);
    va_end(arguments);
    return result;
}

int vasprintf(char **result, const char *restrict format, va_list arguments) {
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(0, 0, format, copy);
    va_end(copy);
    if (length < 0) return -1;
    *result = malloc((size_t)length + 1);
    if (!*result) {
        errno = ENOMEM;
        return -1;
    }
    return vsnprintf(*result, (size_t)length + 1, format, arguments);
}

int asprintf(char **result, const char *restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int length = vasprintf(result, format, arguments);
    va_end(arguments);
    return length;
}
