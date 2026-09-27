#include "internal.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

DIR *opendir(const char *path) {
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return 0;
    DIR *directory = malloc(sizeof(*directory));
    if (!directory) {
        close(fd);
        return 0;
    }
    directory->descriptor = fd;
    return directory;
}
