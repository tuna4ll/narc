#include "internal.h"
#include <errno.h>
#include <unistd.h>

int fseeko(FILE *stream, off_t offset, int origin) {
    if (fflush(stream) != 0) return -1;
    if (lseek(stream->descriptor, offset, origin) < 0) return -1;
    stream->flags &= ~FILE_EOF;
    stream->direction = STREAM_IDLE;
    return 0;
}

int fseek(FILE *stream, long offset, int origin) {
    return fseeko(stream, offset, origin);
}

off_t ftello(FILE *stream) {
    off_t position = lseek(stream->descriptor, 0, SEEK_CUR);
    if (position < 0) return -1;
    if (stream->direction == STREAM_READING)
        position -= (off_t)(stream->read_end - stream->read_pos);
    else if (stream->direction == STREAM_WRITING)
        position += (off_t)stream->write_pos;
    return position;
}

long ftell(FILE *stream) {
    return (long)ftello(stream);
}

void rewind(FILE *stream) {
    fseeko(stream, 0, SEEK_SET);
    stream->flags &= ~FILE_ERROR;
}
