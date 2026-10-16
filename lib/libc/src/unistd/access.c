#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

int access(const char *path, int mode) {
    struct stat info;
    if (stat(path, &info) != 0) return -1;
    if ((mode & X_OK) && !S_ISDIR(info.st_mode) && !(info.st_mode & 0111)) {
        errno = EACCES;
        return -1;
    }
    return 0;
}
