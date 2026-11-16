#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stdint.h>

#define VFS_FILE 1
#define VFS_DIR  2

#define VFS_NAME_LEN 128
#define VFS_PATH_MAX 256

// read returns this when the caller should sleep and retry
#define VFS_AGAIN (-2)

struct vnode;

struct vnode_ops {
    int64_t (*read)(struct vnode *node, uint64_t offset, char *buf, uint64_t len);
    int64_t (*write)(struct vnode *node, uint64_t offset, const char *buf, uint64_t len);
    struct vnode *(*lookup)(struct vnode *dir, const char *name);
    struct vnode *(*readdir)(struct vnode *dir, uint64_t index);
};

struct vnode {
    char name[VFS_NAME_LEN];
    int type;
    uint64_t size;
    const struct vnode_ops *ops;
    const void *data;
    // only used by in memory filesystems
    struct vnode *parent;
};

bool vfs_mount(const char *path, struct vnode *root);
bool vfs_normalize(const char *path, char *out);
struct vnode *vfs_lookup(const char *path);
struct vnode *vfs_child(struct vnode *dir, uint64_t index);
int64_t vfs_read(struct vnode *node, uint64_t offset, char *buf, uint64_t len);
int64_t vfs_write(struct vnode *node, uint64_t offset, const char *buf, uint64_t len);

struct vnode *vfs_table_lookup(struct vnode *nodes, uint64_t count, struct vnode *dir, const char *name);
struct vnode *vfs_table_child(struct vnode *nodes, uint64_t count, struct vnode *dir, uint64_t index);

#endif
