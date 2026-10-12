#pragma once

#include <kernel/mutex.h>
#include <stddef.h>
#include <stdint.h>

#define VFS_NAME_MAX 255
#define VFS_PATH_MAX 4096

#define VFS_REG 1
#define VFS_DIR 2
#define VFS_CHR 3
#define VFS_BLK 4
#define VFS_LNK 5

#define S_IFMT   0170000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000

#define VFS_OPEN_READ      (1u << 0)
#define VFS_OPEN_WRITE     (1u << 1)
#define VFS_OPEN_CREATE    (1u << 2)
#define VFS_OPEN_DIRECTORY (1u << 3)
#define VFS_OPEN_TRUNCATE  (1u << 4)
#define VFS_OPEN_EXCLUSIVE (1u << 5)
#define VFS_OPEN_APPEND    (1u << 6)
#define VFS_OPEN_NONBLOCK  (1u << 7)
#define VFS_STATUS_FLAGS   (VFS_OPEN_READ | VFS_OPEN_WRITE | VFS_OPEN_APPEND | VFS_OPEN_NONBLOCK)

struct vnode;
struct mount;

struct vfs_dirent {
    uint64_t ino;
    uint8_t type;
    char name[VFS_NAME_MAX + 1];
};

struct vnode_ops {
    int (*lookup)(struct vnode *dir, const char *name, size_t length, struct vnode **result);
    int (*create)(struct vnode *dir, const char *name, size_t length, uint32_t mode,
                  uint32_t rdev, struct vnode **result);
    int (*unlink)(struct vnode *dir, const char *name, size_t length, int directory);
    int (*rename)(struct vnode *old_dir, const char *old_name, size_t old_length,
                  struct vnode *new_dir, const char *new_name, size_t new_length);
    int (*readdir)(struct vnode *dir, uint64_t *cookie, struct vfs_dirent *entry);
    long (*read)(struct vnode *node, uint64_t offset, void *buffer, size_t length);
    long (*write)(struct vnode *node, uint64_t offset, const void *buffer, size_t length);
    int (*truncate)(struct vnode *node, uint64_t size);
    void (*release)(struct vnode *node);
};

struct vnode {
    uint32_t refs;
    uint8_t type;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid, gid;
    uint32_t rdev;
    uint64_t ino;
    uint64_t size;
    int64_t atime, mtime, ctime;
    struct mount *mount;
    const struct vnode_ops *ops;
    void *data;
    struct mutex lock;
};

struct mount {
    struct vnode *root;
    void *data;
    uint64_t id;
};

void vnode_init(struct vnode *node, struct mount *mount, const struct vnode_ops *ops,
                uint32_t mode);
struct vnode *vnode_get(struct vnode *node);
void vnode_put(struct vnode *node);
uint8_t vfs_mode_type(uint32_t mode);

int vfs_init(const void *archive, uint64_t size);
struct vnode *vfs_root(void);
int vfs_lookup(struct vnode *base, const char *path, struct vnode **result);
int vfs_open(struct vnode *base, const char *path, uint32_t flags, uint32_t mode,
             struct vnode **result);
int vfs_mkdir(struct vnode *base, const char *path, uint32_t mode);
int vfs_unlink(struct vnode *base, const char *path, int directory);
int vfs_rename(struct vnode *base, const char *old_path, const char *new_path);
long vfs_getcwd(struct vnode *dir, char *buffer, size_t size);
long vfs_read(struct vnode *node, uint64_t offset, void *buffer, size_t length);
long vfs_write(struct vnode *node, uint64_t offset, const void *buffer, size_t length);
int vfs_truncate(struct vnode *node, uint64_t size);
int vfs_readdir(struct vnode *dir, uint64_t *cookie, struct vfs_dirent *entry);

struct vnode *tmpfs_create_root(void);
int tmpfs_attach_archive(struct vnode *node, const void *data, uint64_t size);
int initramfs_unpack(struct vnode *root, const void *archive, uint64_t size);
