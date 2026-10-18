#include "../internal/narc.h"
#include <string.h>
#include <sys/stat.h>

void __stat_convert(const narc_file_info_t *info, struct stat *buffer);

static int stat_path(const char *path, struct stat *buffer, uint32_t flags) {
    narc_file_info_t info;
    if (__libc_result(narc_stat(path, strlen(path), &info, flags)) < 0) return -1;
    __stat_convert(&info, buffer);
    return 0;
}

int stat(const char *path, struct stat *buffer) {
    return stat_path(path, buffer, 0);
}

int lstat(const char *path, struct stat *buffer) {
    return stat_path(path, buffer, NARC_STAT_NOFOLLOW);
}
