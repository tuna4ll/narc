#pragma once

#include <stddef.h>
#include <stdint.h>

#define VFS_NAME_MAX 64
#define VFS_FILE_MAX (64ULL * 1024ULL)

#define VFS_REG 1
#define VFS_DIR 2

#define VFS_OPEN_READ      (1u << 0)
#define VFS_OPEN_WRITE     (1u << 1)
#define VFS_OPEN_CREATE    (1u << 2)
#define VFS_OPEN_DIRECTORY (1u << 3)
#define VFS_OPEN_TRUNCATE  (1u << 4)
#define VFS_OPEN_EXCLUSIVE (1u << 5)
#define VFS_OPEN_APPEND    (1u << 6)

enum vfs_status {
    VFS_OK = 0,
    VFS_NOT_FOUND,
    VFS_EXISTS,
    VFS_NOT_DIRECTORY,
    VFS_IS_DIRECTORY,
    VFS_NO_SPACE,
    VFS_NOT_EMPTY,
    VFS_INVALID,
};

struct vnode;

struct file {
    struct vnode *node;
    uint64_t offset;
    uint32_t flags;
};

struct vfs_info {
    uint64_t size;
    uint64_t ino;
    uint32_t mode;
    uint8_t type;
};

struct vfs_dirent {
    uint64_t ino;
    uint8_t type;
    char name[VFS_NAME_MAX];
};

int vfs_init(const void *archive, uint64_t size);
enum vfs_status vfs_open(const char *path, uint32_t flags, struct file *file);
void vfs_close(struct file *file);
void vfs_file_info(const struct file *file, struct vfs_info *info);
long vfs_read(struct file *file, void *buffer, size_t length);
long vfs_write(struct file *file, const void *buffer, size_t length);
long vfs_seek(struct file *file, int64_t offset, int origin);
int vfs_readdir(struct file *file, struct vfs_dirent *entry);
enum vfs_status vfs_mkdir(const char *path, uint32_t mode);
enum vfs_status vfs_unlink(const char *path, int remove_directory);
enum vfs_status vfs_rename(const char *old_path, const char *new_path);
