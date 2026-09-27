#include "internal.h"
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

size_t fwrite(const void *pointer, size_t size, size_t count, FILE *stream) {
    if (!stream || !(stream->flags & FILE_WRITE)) {
        errno = EBADF;
        if (stream) stream->flags |= FILE_ERROR;
        return 0;
    }
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        stream->flags |= FILE_ERROR;
        return 0;
    }

    const unsigned char *bytes = pointer;
    size_t total = size * count;
    size_t done = 0;
    while (done < total) {
        ssize_t result = write(stream->descriptor, bytes + done, total - done);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            stream->flags |= FILE_ERROR;
            break;
        }
        done += (size_t)result;
    }
    return done / size;
}
