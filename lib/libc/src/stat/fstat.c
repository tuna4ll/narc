#include "../internal/narc.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int fstat(int fd, struct stat *buffer) {
    if (!buffer) {
        errno = EFAULT;
        return -1;
    }
    narc_file_info_t info;
    if (__libc_result(narc_file_info(fd, &info)) < 0) return -1;

    memset(buffer, 0, sizeof(*buffer));
    buffer->st_ino = info.inode;
    buffer->st_nlink = 1;
    buffer->st_mode = info.mode;
    buffer->st_size = (off_t)info.size;
    buffer->st_blksize = 4096;
    buffer->st_blocks = (blkcnt_t)((info.size + 511) / 512);
    return 0;
}
