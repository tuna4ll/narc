#include "internal.h"
#include <errno.h>

void clearerr(FILE *stream) {
    if (stream) stream->flags &= ~(FILE_EOF | FILE_ERROR);
}

int feof(FILE *stream) {
    return stream && (stream->flags & FILE_EOF);
}

int ferror(FILE *stream) {
    return stream && (stream->flags & FILE_ERROR);
}

int fileno(FILE *stream) {
    if (stream) return stream->descriptor;
    errno = EBADF;
    return -1;
}
