#include <kernel/heap.h>
#include <kernel/mutex.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <narc/abi.h>

static struct vnode *root;
static struct mutex namespace_lock = MUTEX_INIT;

uint8_t vfs_mode_type(uint32_t mode) {
    switch (mode & S_IFMT) {
    case S_IFDIR: return VFS_DIR;
    case S_IFCHR: return VFS_CHR;
    case S_IFBLK: return VFS_BLK;
    case S_IFLNK: return VFS_LNK;
    default:      return VFS_REG;
    }
}

void vnode_init(struct vnode *node, struct mount *mount, const struct vnode_ops *ops,
                uint32_t mode) {
    node->mount = mount;
    node->ops = ops;
    node->mode = mode;
    node->type = vfs_mode_type(mode);
    node->nlink = 1;
    mutex_init(&node->lock);
}

struct vnode *vnode_get(struct vnode *node) {
    if (node) __atomic_fetch_add(&node->refs, 1, __ATOMIC_RELAXED);
    return node;
}

void vnode_put(struct vnode *node) {
    if (!node || __atomic_sub_fetch(&node->refs, 1, __ATOMIC_ACQ_REL)) return;
    if (node->ops->release) node->ops->release(node);
}

struct vnode *vfs_root(void) {
    return root;
}

static int dot(const char *name, size_t length) {
    return length == 1 && name[0] == '.';
}

static int dotdot(const char *name, size_t length) {
    return length == 2 && name[0] == '.' && name[1] == '.';
}

static int step(struct vnode **node, const char *name, size_t length) {
    struct vnode *current = *node;
    if (current->type != VFS_DIR) return -NARC_NOT_DIRECTORY;
    if (length > VFS_NAME_MAX) return -NARC_NAME_TOO_LONG;
    if (dot(name, length) || (dotdot(name, length) && current == root)) return 0;
    struct vnode *next;
    int status = current->ops->lookup(current, name, length, &next);
    if (status) return status;
    vnode_put(current);
    *node = next;
    return 0;
}

static int walk(struct vnode *base, const char *path, size_t length, struct vnode **result) {
    struct vnode *node = vnode_get(length && path[0] == '/' ? root : base);
    for (size_t i = 0; i < length;) {
        while (i < length && path[i] == '/') i++;
        size_t start = i;
        while (i < length && path[i] != '/') i++;
        if (i == start) break;
        int status = step(&node, path + start, i - start);
        if (status) {
            vnode_put(node);
            return status;
        }
    }
    if (length && path[length - 1] == '/' && node->type != VFS_DIR) {
        vnode_put(node);
        return -NARC_NOT_DIRECTORY;
    }
    *result = node;
    return 0;
}

static int lookup_parent(struct vnode *base, const char *path, struct vnode **parent,
                         const char **name, size_t *length) {
    size_t end = strlen(path);
    while (end && path[end - 1] == '/') end--;
    size_t start = end;
    while (start && path[start - 1] != '/') start--;
    if (start == end || dot(path + start, end - start) || dotdot(path + start, end - start))
        return -NARC_INVALID_ARGUMENT;
    if (end - start > VFS_NAME_MAX) return -NARC_NAME_TOO_LONG;
    int status = walk(base, path, start, parent);
    if (status) return status;
    if ((*parent)->type != VFS_DIR) {
        vnode_put(*parent);
        return -NARC_NOT_DIRECTORY;
    }
    *name = path + start;
    *length = end - start;
    return 0;
}

int vfs_lookup(struct vnode *base, const char *path, struct vnode **result) {
    if (!*path) return -NARC_NOT_FOUND;
    mutex_lock(&namespace_lock);
    int status = walk(base, path, strlen(path), result);
    mutex_unlock(&namespace_lock);
    return status;
}

static int create_locked(struct vnode *base, const char *path, uint32_t mode,
                         struct vnode **result) {
    struct vnode *parent;
    const char *name;
    size_t length;
    int status = lookup_parent(base, path, &parent, &name, &length);
    if (status) return status;
    status = parent->ops->create ? parent->ops->create(parent, name, length, mode, 0, result) :
                                   -NARC_NOT_SUPPORTED;
    vnode_put(parent);
    return status;
}

static int open_locked(struct vnode *base, const char *path, uint32_t flags, uint32_t mode,
                       struct vnode **result) {
    int status = walk(base, path, strlen(path), result);
    if (status == -NARC_NOT_FOUND && (flags & VFS_OPEN_CREATE))
        return create_locked(base, path, S_IFREG | (mode & 07777), result);
    if (status) return status;
    if ((flags & VFS_OPEN_CREATE) && (flags & VFS_OPEN_EXCLUSIVE)) status = -NARC_EXISTS;
    else if ((flags & VFS_OPEN_DIRECTORY) && (*result)->type != VFS_DIR)
        status = -NARC_NOT_DIRECTORY;
    else if ((flags & VFS_OPEN_WRITE) && (*result)->type == VFS_DIR)
        status = -NARC_IS_DIRECTORY;
    if (status) vnode_put(*result);
    return status;
}

int vfs_open(struct vnode *base, const char *path, uint32_t flags, uint32_t mode,
             struct vnode **result) {
    if (!*path) return -NARC_NOT_FOUND;
    mutex_lock(&namespace_lock);
    int status = open_locked(base, path, flags, mode, result);
    mutex_unlock(&namespace_lock);
    if (status || !(flags & VFS_OPEN_TRUNCATE) || !(flags & VFS_OPEN_WRITE) ||
        (*result)->type != VFS_REG)
        return status;
    status = vfs_truncate(*result, 0);
    if (status) vnode_put(*result);
    return status;
}

int vfs_mkdir(struct vnode *base, const char *path, uint32_t mode) {
    mutex_lock(&namespace_lock);
    int status = create_locked(base, path, S_IFDIR | (mode & 07777), 0);
    mutex_unlock(&namespace_lock);
    return status;
}

int vfs_unlink(struct vnode *base, const char *path, int directory) {
    struct vnode *parent;
    const char *name;
    size_t length;
    mutex_lock(&namespace_lock);
    int status = lookup_parent(base, path, &parent, &name, &length);
    if (!status) {
        status = parent->ops->unlink ? parent->ops->unlink(parent, name, length, directory) :
                                       -NARC_NOT_SUPPORTED;
        vnode_put(parent);
    }
    mutex_unlock(&namespace_lock);
    return status;
}

static int contains(struct vnode *ancestor, struct vnode *dir) {
    struct vnode *node = vnode_get(dir);
    int found = 0;
    while (!found && node != root) {
        found = node == ancestor;
        if (step(&node, "..", 2)) break;
    }
    vnode_put(node);
    return found;
}

static int rename_locked(struct vnode *base, const char *old_path, const char *new_path) {
    struct vnode *old_parent, *new_parent, *node;
    const char *old_name, *new_name;
    size_t old_length, new_length;
    int status = lookup_parent(base, old_path, &old_parent, &old_name, &old_length);
    if (status) return status;
    status = lookup_parent(base, new_path, &new_parent, &new_name, &new_length);
    if (status) {
        vnode_put(old_parent);
        return status;
    }
    status = old_parent->ops->lookup(old_parent, old_name, old_length, &node);
    if (!status) {
        if (node->type == VFS_DIR && contains(node, new_parent)) status = -NARC_INVALID_ARGUMENT;
        vnode_put(node);
    }
    if (!status && old_parent->mount != new_parent->mount) status = -NARC_CROSS_DEVICE;
    if (!status)
        status = old_parent->ops->rename ?
                 old_parent->ops->rename(old_parent, old_name, old_length,
                                         new_parent, new_name, new_length) :
                 -NARC_NOT_SUPPORTED;
    vnode_put(old_parent);
    vnode_put(new_parent);
    return status;
}

int vfs_rename(struct vnode *base, const char *old_path, const char *new_path) {
    mutex_lock(&namespace_lock);
    int status = rename_locked(base, old_path, new_path);
    mutex_unlock(&namespace_lock);
    return status;
}

static int entry_name(struct vnode *parent, struct vnode *node, struct vfs_dirent *entry) {
    uint64_t cookie = 0;
    int status;
    while ((status = parent->ops->readdir(parent, &cookie, entry)) > 0) {
        size_t length = strlen(entry->name);
        if (entry->ino == node->ino && !dot(entry->name, length) &&
            !dotdot(entry->name, length))
            return 0;
    }
    return status < 0 ? status : -NARC_NOT_FOUND;
}

static long getcwd_locked(struct vnode *dir, char *buffer, size_t size) {
    struct vfs_dirent *entry = kmalloc(sizeof(*entry));
    if (!entry) return -NARC_NO_MEMORY;
    size_t position = size;
    buffer[--position] = 0;
    struct vnode *node = vnode_get(dir);
    long status = 0;
    while (!status && node != root) {
        struct vnode *parent;
        status = node->ops->lookup(node, "..", 2, &parent);
        if (status) break;
        status = entry_name(parent, node, entry);
        size_t length = status ? 0 : strlen(entry->name);
        if (!status && length + 1 > position) status = -NARC_OUT_OF_RANGE;
        if (!status) {
            position -= length;
            memcpy(buffer + position, entry->name, length);
            buffer[--position] = '/';
        }
        vnode_put(node);
        node = parent;
    }
    vnode_put(node);
    kfree(entry);
    if (status) return status;
    if (position == size - 1) {
        if (!position) return -NARC_OUT_OF_RANGE;
        buffer[--position] = '/';
    }
    memmove(buffer, buffer + position, size - position);
    return (long)(size - position - 1);
}

long vfs_getcwd(struct vnode *dir, char *buffer, size_t size) {
    if (!size) return -NARC_OUT_OF_RANGE;
    mutex_lock(&namespace_lock);
    long status = getcwd_locked(dir, buffer, size);
    mutex_unlock(&namespace_lock);
    return status;
}

long vfs_read(struct vnode *node, uint64_t offset, void *buffer, size_t length) {
    if (node->type == VFS_DIR) return -NARC_IS_DIRECTORY;
    if (node->type != VFS_REG || !node->ops->read) return -NARC_INVALID_ARGUMENT;
    mutex_lock(&node->lock);
    long result = node->ops->read(node, offset, buffer, length);
    mutex_unlock(&node->lock);
    return result;
}

long vfs_write(struct vnode *node, uint64_t offset, const void *buffer, size_t length) {
    if (node->type == VFS_DIR) return -NARC_IS_DIRECTORY;
    if (node->type != VFS_REG || !node->ops->write) return -NARC_INVALID_ARGUMENT;
    mutex_lock(&node->lock);
    long result = node->ops->write(node, offset, buffer, length);
    mutex_unlock(&node->lock);
    return result;
}

int vfs_truncate(struct vnode *node, uint64_t size) {
    if (node->type == VFS_DIR) return -NARC_IS_DIRECTORY;
    if (node->type != VFS_REG || !node->ops->truncate) return -NARC_INVALID_ARGUMENT;
    mutex_lock(&node->lock);
    int status = node->ops->truncate(node, size);
    mutex_unlock(&node->lock);
    return status;
}

int vfs_readdir(struct vnode *dir, uint64_t *cookie, struct vfs_dirent *entry) {
    if (dir->type != VFS_DIR) return -NARC_NOT_DIRECTORY;
    mutex_lock(&namespace_lock);
    int result = dir->ops->readdir(dir, cookie, entry);
    mutex_unlock(&namespace_lock);
    return result;
}

int vfs_init(const void *archive, uint64_t size) {
    root = tmpfs_create_root();
    if (!root) return -1;
    return initramfs_unpack(root, archive, size) == 0 ? 0 : -1;
}
