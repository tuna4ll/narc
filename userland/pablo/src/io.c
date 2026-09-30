#include <errno.h>
#include <fcntl.h>
#include <pablo/io.h>
#include <unistd.h>

int pablo_read_file(const char *path, char *buffer, size_t capacity, size_t *length) {
    if (!capacity) {
        errno = EINVAL;
        return -1;
    }
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    size_t used = 0;
    while (used + 1 < capacity) {
        ssize_t count = read(descriptor, buffer + used, capacity - used - 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            close(descriptor);
            return -1;
        }
        if (!count) break;
        used += (size_t)count;
    }
    char extra;
    ssize_t overflow = read(descriptor, &extra, 1);
    int close_result = close(descriptor);
    if (overflow > 0) {
        errno = EOVERFLOW;
        return -1;
    }
    if (overflow < 0 || close_result != 0) return -1;
    buffer[used] = 0;
    *length = used;
    return 0;
}
