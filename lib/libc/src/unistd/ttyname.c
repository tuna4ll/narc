#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int ttyname_r(int fd, char *buffer, size_t size) {
    struct stat target;
    if (fstat(fd, &target) != 0) return errno;
    if (!isatty(fd)) return ENOTTY;
    DIR *directory = opendir("/dev");
    if (!directory) return errno;
    int status = ENOTTY;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        char path[PATH_MAX];
        struct stat candidate;
        if (entry->d_name[0] == '.') continue;
        if ((size_t)snprintf(path, sizeof(path), "/dev/%s", entry->d_name) >= sizeof(path)) continue;
        if (stat(path, &candidate) != 0 || !S_ISCHR(candidate.st_mode)) continue;
        if (candidate.st_rdev != target.st_rdev || !strcmp(entry->d_name, "tty")) continue;
        if (strlen(path) >= size) {
            status = ERANGE;
            break;
        }
        strcpy(buffer, path);
        status = 0;
        break;
    }
    closedir(directory);
    return status;
}

char *ttyname(int fd) {
    static char name[PATH_MAX];
    int status = ttyname_r(fd, name, sizeof(name));
    if (status) {
        errno = status;
        return 0;
    }
    return name;
}
