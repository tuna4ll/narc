#include "../internal/narc.h"
#include <string.h>
#include <sys/statvfs.h>

static void convert(const narc_fs_info_t *info, struct statvfs *buffer) {
    memset(buffer, 0, sizeof(*buffer));
    buffer->f_bsize = buffer->f_frsize = (unsigned long)info->block_size;
    buffer->f_blocks = info->blocks;
    buffer->f_bfree = info->free_blocks;
    buffer->f_bavail = info->available_blocks;
    buffer->f_files = info->files;
    buffer->f_ffree = buffer->f_favail = info->free_files;
    buffer->f_fsid = (unsigned long)info->device;
    buffer->f_flag = (info->flags & NARC_MOUNT_READONLY) ? ST_RDONLY : 0;
    buffer->f_namemax = info->name_max;
    memcpy(buffer->f_basetype, info->type, sizeof(buffer->f_basetype));
    buffer->f_basetype[sizeof(buffer->f_basetype) - 1] = 0;
}

int statvfs(const char *restrict path, struct statvfs *restrict buffer) {
    narc_fs_info_t info;
    if (__libc_result(narc_fs_info(path, strlen(path), &info)) < 0) return -1;
    convert(&info, buffer);
    return 0;
}

int fstatvfs(int fd, struct statvfs *buffer) {
    narc_fs_info_t info;
    if (__libc_result(narc_file_fs_info(fd, &info)) < 0) return -1;
    convert(&info, buffer);
    return 0;
}
