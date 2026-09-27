#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int stat(const char *path, struct stat *buffer) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int result = fstat(fd, buffer);
    close(fd);
    return result;
}
