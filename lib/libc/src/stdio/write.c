#include "internal.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static size_t write_direct(FILE *stream, const unsigned char *data, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t result = write(stream->descriptor, data + done, length - done);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            stream->flags |= FILE_ERROR;
            break;
        }
        done += (size_t)result;
    }
    return done;
}

size_t fwrite(const void *restrict pointer, size_t size, size_t count, FILE *restrict stream) {
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        stream->flags |= FILE_ERROR;
        return 0;
    }
    if (__stdio_prepare_write(stream) != 0) return 0;
    const unsigned char *data = pointer;
    size_t total = size * count;
    if (!stream->buffer) return write_direct(stream, data, total) / size;
    size_t done = 0;
    while (done < total) {
        if (stream->write_pos == stream->size && __stdio_flush_write(stream) != 0) break;
        if (!stream->write_pos && total - done >= stream->size) {
            done += write_direct(stream, data + done, total - done);
            break;
        }
        size_t chunk = stream->size - stream->write_pos;
        if (chunk > total - done) chunk = total - done;
        memcpy(stream->buffer + stream->write_pos, data + done, chunk);
        stream->write_pos += chunk;
        done += chunk;
    }
    if (stream->mode == _IOLBF && memchr(data, '\n', total)) __stdio_flush_write(stream);
    return done / size;
}

int fputc(int character, FILE *stream) {
    unsigned char byte = (unsigned char)character;
    return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

int putc(int character, FILE *stream) {
    return fputc(character, stream);
}

int putchar(int character) {
    return fputc(character, stdout);
}

int fputs(const char *restrict string, FILE *restrict stream) {
    size_t length = strlen(string);
    return fwrite(string, 1, length, stream) == length ? 0 : EOF;
}

int puts(const char *string) {
    if (fputs(string, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}
