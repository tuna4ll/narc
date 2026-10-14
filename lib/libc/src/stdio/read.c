#include "internal.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int fgetc(FILE *stream) {
    if (__stdio_prepare_read(stream) != 0) return EOF;
    if (stream->read_pos == stream->read_end) {
        if (!stream->buffer) {
            unsigned char byte;
            ssize_t result;
            do result = read(stream->descriptor, &byte, 1);
            while (result < 0 && errno == EINTR);
            if (result == 1) return byte;
            stream->flags |= result ? FILE_ERROR : FILE_EOF;
            return EOF;
        }
        if (__stdio_fill(stream) != 0) return EOF;
    }
    return stream->buffer[stream->read_pos++];
}

int getc(FILE *stream) {
    return fgetc(stream);
}

int getchar(void) {
    return fgetc(stdin);
}

int ungetc(int character, FILE *stream) {
    if (character == EOF || __stdio_prepare_read(stream) != 0 || !stream->buffer) return EOF;
    if (!stream->read_pos) {
        size_t unread = stream->read_end - stream->read_pos;
        if (unread == stream->size) return EOF;
        memmove(stream->buffer + 1, stream->buffer, unread);
        stream->read_end = unread + 1;
        stream->read_pos = 1;
    }
    stream->buffer[--stream->read_pos] = (unsigned char)character;
    stream->flags &= ~FILE_EOF;
    return (unsigned char)character;
}

size_t fread(void *restrict pointer, size_t size, size_t count, FILE *restrict stream) {
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        return 0;
    }
    unsigned char *out = pointer;
    size_t total = size * count, done = 0;
    while (done < total) {
        if (__stdio_prepare_read(stream) != 0) break;
        if (stream->buffer && stream->read_pos < stream->read_end) {
            size_t chunk = stream->read_end - stream->read_pos;
            if (chunk > total - done) chunk = total - done;
            memcpy(out + done, stream->buffer + stream->read_pos, chunk);
            stream->read_pos += chunk;
            done += chunk;
            continue;
        }
        if (!stream->buffer || total - done >= stream->size) {
            ssize_t result = read(stream->descriptor, out + done, total - done);
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) {
                stream->flags |= result ? FILE_ERROR : FILE_EOF;
                break;
            }
            done += (size_t)result;
            continue;
        }
        if (__stdio_fill(stream) != 0) break;
    }
    return done / size;
}

ssize_t getdelim(char **restrict line, size_t *restrict capacity, int delimiter,
                 FILE *restrict stream) {
    if (!line || !capacity) {
        errno = EINVAL;
        return -1;
    }
    size_t length = 0;
    for (;;) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (!length || ferror(stream)) return -1;
            break;
        }
        if (length + 2 > *capacity) {
            size_t next = *capacity ? *capacity * 2 : 128;
            char *grown = realloc(*line, next);
            if (!grown) {
                errno = ENOMEM;
                return -1;
            }
            *line = grown;
            *capacity = next;
        }
        (*line)[length++] = (char)c;
        if (c == delimiter) break;
    }
    (*line)[length] = 0;
    return (ssize_t)length;
}

ssize_t getline(char **restrict line, size_t *restrict capacity, FILE *restrict stream) {
    return getdelim(line, capacity, '\n', stream);
}

char *fgets(char *restrict buffer, int size, FILE *restrict stream) {
    if (size <= 0) return 0;
    int length = 0;
    while (length < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) break;
        buffer[length++] = (char)c;
        if (c == '\n') break;
    }
    if (!length) return 0;
    buffer[length] = 0;
    return buffer;
}
