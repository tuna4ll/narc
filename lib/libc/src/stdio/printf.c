#include "internal.h"
#include <errno.h>
#include <unistd.h>

static void put_stream(struct format_sink *sink, const char *data, size_t length) {
    if (fwrite(data, 1, length, sink->context) != length) sink->failed = EIO;
}

int vfprintf(FILE *restrict stream, const char *restrict format, va_list arguments) {
    struct format_sink sink = { put_stream, stream, 0, 0 };
    return __stdio_format(&sink, format, arguments);
}

int fprintf(FILE *restrict stream, const char *restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int vprintf(const char *restrict format, va_list arguments) {
    return vfprintf(stdout, format, arguments);
}

int printf(const char *restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

struct descriptor_sink {
    int fd;
    char buffer[256];
    size_t used;
};

static int drain(struct descriptor_sink *out) {
    size_t done = 0;
    while (done < out->used) {
        ssize_t result = write(out->fd, out->buffer + done, out->used - done);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return -1;
        done += (size_t)result;
    }
    out->used = 0;
    return 0;
}

static void put_descriptor(struct format_sink *sink, const char *data, size_t length) {
    struct descriptor_sink *out = sink->context;
    for (size_t i = 0; i < length; i++) {
        if (out->used == sizeof(out->buffer) && drain(out) != 0) {
            sink->failed = EIO;
            return;
        }
        out->buffer[out->used++] = data[i];
    }
}

int vdprintf(int fd, const char *restrict format, va_list arguments) {
    struct descriptor_sink out = { .fd = fd };
    struct format_sink sink = { put_descriptor, &out, 0, 0 };
    int result = __stdio_format(&sink, format, arguments);
    if (drain(&out) != 0) return -1;
    return result;
}

int dprintf(int fd, const char *restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vdprintf(fd, format, arguments);
    va_end(arguments);
    return result;
}
