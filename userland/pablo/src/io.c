#include <errno.h>
#include <fcntl.h>
#include <pablo/io.h>
#include <stdlib.h>
#include <unistd.h>

int pablo_read_file(const char *path, char **buffer, size_t *length) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    size_t used = 0, capacity = 0;
    char *data = 0;
    for (;;) {
        if (used + 1 >= capacity) {
            size_t next = capacity ? capacity * 2 : 1024;
            char *grown = next > capacity ? realloc(data, next) : 0;
            if (!grown) {
                free(data);
                close(descriptor);
                errno = ENOMEM;
                return -1;
            }
            data = grown;
            capacity = next;
        }
        ssize_t count = read(descriptor, data + used, capacity - used - 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            free(data);
            close(descriptor);
            return -1;
        }
        if (!count) break;
        used += (size_t)count;
    }
    if (close(descriptor) != 0) {
        free(data);
        return -1;
    }
    data[used] = 0;
    *buffer = data;
    *length = used;
    return 0;
}
