#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int __stdio_mode_flags(const char *mode, int *flags) {
    int access;
    switch (mode[0]) {
    case 'r': access = O_RDONLY; *flags = 0; break;
    case 'w': access = O_WRONLY; *flags = O_CREAT | O_TRUNC; break;
    case 'a': access = O_WRONLY; *flags = O_CREAT | O_APPEND; break;
    default:
        errno = EINVAL;
        return -1;
    }
    for (const char *c = mode + 1; *c; c++) {
        if (*c == '+') access = O_RDWR;
        else if (*c == 'x') *flags |= O_EXCL;
        else if (*c == 'e') *flags |= O_CLOEXEC;
    }
    *flags |= access;
    return 0;
}

static FILE *make_stream(int fd, int flags) {
    FILE *stream = calloc(1, sizeof(*stream));
    if (!stream) return 0;
    stream->descriptor = fd;
    int access = flags & O_ACCMODE;
    if (access == O_RDONLY || access == O_RDWR) stream->flags |= FILE_READ;
    if (access == O_WRONLY || access == O_RDWR) stream->flags |= FILE_WRITE;
    stream->mode = isatty(fd) ? _IOLBF : _IOFBF;
    __stdio_register(stream);
    return stream;
}

FILE *fopen(const char *restrict path, const char *restrict mode) {
    int flags;
    if (__stdio_mode_flags(mode, &flags) != 0) return 0;
    int fd = open(path, flags, 0666);
    if (fd < 0) return 0;
    FILE *stream = make_stream(fd, flags);
    if (!stream) close(fd);
    return stream;
}

FILE *fdopen(int fd, const char *mode) {
    int flags;
    if (__stdio_mode_flags(mode, &flags) != 0) return 0;
    if (fcntl(fd, F_GETFD) < 0) return 0;
    return make_stream(fd, flags);
}

int fclose(FILE *stream) {
    int status = fflush(stream);
    if (close(stream->descriptor) != 0) status = EOF;
    if (stream->flags & FILE_OWNED) free(stream->buffer);
    if (stream->flags & FILE_STATIC) {
        stream->buffer = 0;
        stream->flags &= ~FILE_OWNED;
        stream->flags |= FILE_ERROR;
        return status;
    }
    __stdio_unregister(stream);
    free(stream);
    return status;
}

FILE *freopen(const char *restrict path, const char *restrict mode, FILE *restrict stream) {
    int flags;
    fflush(stream);
    if (__stdio_mode_flags(mode, &flags) != 0) return 0;
    int fd = open(path, flags, 0666);
    if (fd < 0) return 0;
    if (dup2(fd, stream->descriptor) < 0) {
        close(fd);
        return 0;
    }
    close(fd);
    int access = flags & O_ACCMODE;
    stream->flags &= ~(FILE_READ | FILE_WRITE | FILE_EOF | FILE_ERROR);
    if (access == O_RDONLY || access == O_RDWR) stream->flags |= FILE_READ;
    if (access == O_WRONLY || access == O_RDWR) stream->flags |= FILE_WRITE;
    stream->read_pos = stream->read_end = stream->write_pos = 0;
    stream->direction = STREAM_IDLE;
    return stream;
}
