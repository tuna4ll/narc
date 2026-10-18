#include "../internal/narc.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

void __stat_convert(const narc_file_info_t *info, struct stat *buffer) {
    memset(buffer, 0, sizeof(*buffer));
    buffer->st_dev = info->device;
    buffer->st_ino = info->inode;
    buffer->st_nlink = info->nlink;
    buffer->st_mode = info->mode;
    buffer->st_uid = info->uid;
    buffer->st_gid = info->gid;
    buffer->st_rdev = info->rdev;
    buffer->st_size = (off_t)info->size;
    buffer->st_blksize = 4096;
    buffer->st_blocks = (blkcnt_t)info->blocks;
    buffer->st_atim.tv_sec = info->atime;
    buffer->st_mtim.tv_sec = info->mtime;
    buffer->st_ctim.tv_sec = info->ctime;
}

int fstat(int fd, struct stat *buffer) {
    if (!buffer) {
        errno = EFAULT;
        return -1;
    }
    narc_file_info_t info;
    if (__libc_result(narc_file_info(fd, &info)) < 0) return -1;
    __stat_convert(&info, buffer);
    return 0;
}
