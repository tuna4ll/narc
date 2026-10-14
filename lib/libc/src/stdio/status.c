#include "internal.h"
#include <errno.h>

void clearerr(FILE *stream) {
    stream->flags &= ~(FILE_EOF | FILE_ERROR);
}

int feof(FILE *stream) {
    return (stream->flags & FILE_EOF) != 0;
}

int ferror(FILE *stream) {
    return (stream->flags & FILE_ERROR) != 0;
}

int fileno(FILE *stream) {
    return stream->descriptor;
}
