#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static FILE standard_streams[3] = {
    { 0, FILE_READ | FILE_STATIC | FILE_DETECT, _IOLBF, 0, 0, 0, 0, 0, STREAM_IDLE,
      &standard_streams[1] },
    { 1, FILE_WRITE | FILE_STATIC | FILE_DETECT, _IOFBF, 0, 0, 0, 0, 0, STREAM_IDLE,
      &standard_streams[2] },
    { 2, FILE_WRITE | FILE_STATIC, _IONBF, 0, 0, 0, 0, 0, STREAM_IDLE, 0 },
};

FILE *stdin = &standard_streams[0];
FILE *stdout = &standard_streams[1];
FILE *stderr = &standard_streams[2];

static FILE *streams = &standard_streams[0];

void __stdio_register(FILE *stream) {
    stream->next = streams;
    streams = stream;
}

void __stdio_unregister(FILE *stream) {
    for (FILE **link = &streams; *link; link = &(*link)->next) {
        if (*link != stream) continue;
        *link = stream->next;
        return;
    }
}

static int ensure_buffer(FILE *stream) {
    if (stream->flags & FILE_DETECT) {
        stream->flags &= ~FILE_DETECT;
        if (stream->descriptor != 0) stream->mode = isatty(stream->descriptor) ? _IOLBF : _IOFBF;
    }
    if (stream->buffer || stream->mode == _IONBF) return 0;
    stream->buffer = malloc(BUFSIZ);
    if (!stream->buffer) {
        stream->mode = _IONBF;
        return 0;
    }
    stream->size = BUFSIZ;
    stream->flags |= FILE_OWNED;
    return 0;
}

int __stdio_flush_write(FILE *stream) {
    size_t done = 0;
    while (done < stream->write_pos) {
        ssize_t result = write(stream->descriptor, stream->buffer + done, stream->write_pos - done);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            stream->flags |= FILE_ERROR;
            if (done) {
                for (size_t i = done; i < stream->write_pos; i++)
                    stream->buffer[i - done] = stream->buffer[i];
                stream->write_pos -= done;
            }
            return EOF;
        }
        done += (size_t)result;
    }
    stream->write_pos = 0;
    return 0;
}

int __stdio_prepare_write(FILE *stream) {
    if (!(stream->flags & FILE_WRITE)) {
        stream->flags |= FILE_ERROR;
        errno = EBADF;
        return EOF;
    }
    if (stream->direction == STREAM_READING) {
        size_t unread = stream->read_end - stream->read_pos;
        if (unread) lseek(stream->descriptor, -(off_t)unread, SEEK_CUR);
        stream->read_pos = stream->read_end = 0;
    }
    stream->direction = STREAM_WRITING;
    return ensure_buffer(stream);
}

static void flush_line_buffered(void) {
    for (FILE *stream = streams; stream; stream = stream->next)
        if (stream->mode == _IOLBF && stream->direction == STREAM_WRITING && stream->write_pos)
            __stdio_flush_write(stream);
}

int __stdio_prepare_read(FILE *stream) {
    if (!(stream->flags & FILE_READ)) {
        stream->flags |= FILE_ERROR;
        errno = EBADF;
        return EOF;
    }
    if (stream->direction == STREAM_WRITING && __stdio_flush_write(stream) != 0) return EOF;
    stream->direction = STREAM_READING;
    return ensure_buffer(stream);
}

int __stdio_fill(FILE *stream) {
    if (stream->mode != _IOFBF) {
        if (stdout->direction == STREAM_WRITING && stdout->write_pos) __stdio_flush_write(stdout);
        flush_line_buffered();
    }
    unsigned char *target = stream->buffer;
    size_t capacity = stream->size;
    for (;;) {
        ssize_t result = read(stream->descriptor, target, capacity);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) {
            stream->flags |= FILE_ERROR;
            return EOF;
        }
        if (!result) {
            stream->flags |= FILE_EOF;
            return EOF;
        }
        stream->read_pos = 0;
        stream->read_end = (size_t)result;
        return 0;
    }
}

int fflush(FILE *stream) {
    if (!stream) {
        int status = 0;
        for (FILE *cursor = streams; cursor; cursor = cursor->next)
            if (cursor->direction == STREAM_WRITING && fflush(cursor) != 0) status = EOF;
        return status;
    }
    if (stream->direction == STREAM_WRITING) return __stdio_flush_write(stream);
    if (stream->direction == STREAM_READING) {
        size_t unread = stream->read_end - stream->read_pos;
        if (unread) lseek(stream->descriptor, -(off_t)unread, SEEK_CUR);
        stream->read_pos = stream->read_end = 0;
        stream->direction = STREAM_IDLE;
    }
    return 0;
}

void __stdio_exit(void) {
    fflush(0);
}

int setvbuf(FILE *restrict stream, char *restrict buffer, int mode, size_t size) {
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) {
        errno = EINVAL;
        return -1;
    }
    fflush(stream);
    if (stream->flags & FILE_OWNED) free(stream->buffer);
    stream->flags &= ~(FILE_OWNED | FILE_DETECT);
    stream->mode = mode;
    stream->buffer = 0;
    stream->size = 0;
    if (mode != _IONBF && buffer && size) {
        stream->buffer = (unsigned char *)buffer;
        stream->size = size;
    }
    return 0;
}

void setbuf(FILE *restrict stream, char *restrict buffer) {
    setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, BUFSIZ);
}

void setlinebuf(FILE *stream) {
    setvbuf(stream, 0, _IOLBF, 0);
}
