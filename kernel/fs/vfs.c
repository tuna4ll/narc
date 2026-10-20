#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/mutex.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <kernel/vfs.h>
#include <narc/abi.h>

static struct vnode *root;
static struct mutex namespace_lock = MUTEX_INIT;
static struct filesystem *filesystems;
static struct mount *mounts;
static uint64_t next_mount_id = 1;

void vfs_register(struct filesystem *fs) {
    fs->next = filesystems;
    filesystems = fs;
}

struct mount *vfs_mount_alloc(const struct filesystem *fs) {
    struct mount *mount = kzalloc(sizeof(*mount));
    if (!mount) return 0;
    mount->fs = fs;
    mount->id = next_mount_id++;
    return mount;
}

static const struct filesystem *find_filesystem(const char *name) {
    for (struct filesystem *fs = filesystems; fs; fs = fs->next)
        if (!strcmp(fs->name, name)) return fs;
    return 0;
}

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

static int read_only(struct vnode *node) {
    return (node->mount->flags & VFS_MOUNT_READONLY) != 0;
}

static int dot(const char *name, size_t length) {
    return length == 1 && name[0] == '.';
}

static int dotdot(const char *name, size_t length) {
    return length == 2 && name[0] == '.' && name[1] == '.';
}

static struct vnode *cross_down(struct vnode *node) {
    while (node->mounted) {
        struct vnode *next = vnode_get(node->mounted->root);
        vnode_put(node);
        node = next;
    }
    return node;
}

static struct vnode *cross_up(struct vnode *node) {
    while (node != root && node == node->mount->root && node->mount->covered) {
        struct vnode *next = vnode_get(node->mount->covered);
        vnode_put(node);
        node = next;
    }
    return node;
}

static int step(struct vnode **node, const char *name, size_t length) {
    if ((*node)->type != VFS_DIR) return -NARC_NOT_DIRECTORY;
    if (length > VFS_NAME_MAX) return -NARC_NAME_TOO_LONG;
    if (dot(name, length) || (dotdot(name, length) && *node == root)) return 0;
    if (dotdot(name, length)) *node = cross_up(*node);
    struct vnode *current = *node;
    struct vnode *next;
    int status = current->ops->lookup(current, name, length, &next);
    if (status) return status;
    vnode_put(current);
    *node = cross_down(next);
    return 0;
}

#define SYMLINK_DEPTH 40

static int walk_depth(struct vnode *base, const char *path, size_t length, int follow,
                      int *depth, struct vnode **result);

static int follow_link(struct vnode *dir, struct vnode *link, int *depth, struct vnode **result) {
    if (++*depth > SYMLINK_DEPTH) return -NARC_LOOP;
    if (!link->ops->readlink) return -NARC_INVALID_ARGUMENT;
    char *target = kmalloc(VFS_PATH_MAX);
    if (!target) return -NARC_NO_MEMORY;
    long length = link->ops->readlink(link, target, VFS_PATH_MAX - 1);
    int status = length < 0 ? (int)length : 0;
    if (!status) status = walk_depth(dir, target, (size_t)length, 1, depth, result);
    kfree(target);
    return status;
}

static int walk_depth(struct vnode *base, const char *path, size_t length, int follow,
                      int *depth, struct vnode **result) {
    if (!length) return -NARC_NOT_FOUND;
    struct vnode *node = vnode_get(path[0] == '/' ? root : base);
    for (size_t i = 0; i < length;) {
        while (i < length && path[i] == '/') i++;
        size_t start = i;
        while (i < length && path[i] != '/') i++;
        if (i == start) break;
        struct vnode *parent = vnode_get(node);
        int status = step(&node, path + start, i - start);
        if (!status && node->type == VFS_LNK && (follow || i < length)) {
            struct vnode *target;
            status = follow_link(parent, node, depth, &target);
            if (!status) {
                vnode_put(node);
                node = target;
            }
        }
        vnode_put(parent);
        if (status) {
            vnode_put(node);
            return status;
        }
    }
    if (path[length - 1] == '/' && node->type != VFS_DIR) {
        vnode_put(node);
        return -NARC_NOT_DIRECTORY;
    }
    *result = node;
    return 0;
}

static int walk_mode(struct vnode *base, const char *path, size_t length, int follow,
                     struct vnode **result) {
    int depth = 0;
    return walk_depth(base, path, length, follow, &depth, result);
}

static int walk(struct vnode *base, const char *path, size_t length, struct vnode **result) {
    if (!length) {
        *result = vnode_get(base);
        return 0;
    }
    return walk_mode(base, path, length, 1, result);
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

int vfs_lookup_nofollow(struct vnode *base, const char *path, struct vnode **result) {
    if (!*path) return -NARC_NOT_FOUND;
    mutex_lock(&namespace_lock);
    int status = walk_mode(base, path, strlen(path), 0, result);
    mutex_unlock(&namespace_lock);
    return status;
}

int vfs_symlink(struct vnode *base, const char *target, const char *path) {
    struct vnode *parent;
    const char *name;
    size_t length;
    mutex_lock(&namespace_lock);
    int status = lookup_parent(base, path, &parent, &name, &length);
    if (!status) {
        if (read_only(parent)) status = -NARC_READ_ONLY_FS;
        else status = parent->ops->symlink ? parent->ops->symlink(parent, name, length, target) :
                                             -NARC_NOT_SUPPORTED;
        vnode_put(parent);
    }
    mutex_unlock(&namespace_lock);
    return status;
}

long vfs_readlink(struct vnode *base, const char *path, char *buffer, size_t size) {
    struct vnode *node;
    int status = vfs_lookup_nofollow(base, path, &node);
    if (status) return status;
    long result = node->type != VFS_LNK || !node->ops->readlink ? -NARC_INVALID_ARGUMENT :
                  node->ops->readlink(node, buffer, size);
    vnode_put(node);
    return result;
}

int vfs_link(struct vnode *base, const char *old_path, const char *new_path) {
    struct vnode *node, *parent;
    const char *name;
    size_t length;
    mutex_lock(&namespace_lock);
    int status = walk_mode(base, old_path, strlen(old_path), 0, &node);
    if (status) {
        mutex_unlock(&namespace_lock);
        return status;
    }
    status = lookup_parent(base, new_path, &parent, &name, &length);
    if (!status) {
        if (node->type == VFS_DIR) status = -NARC_NOT_PERMITTED;
        else if (parent->mount != node->mount) status = -NARC_CROSS_DEVICE;
        else if (read_only(parent)) status = -NARC_READ_ONLY_FS;
        else status = parent->ops->link ? parent->ops->link(parent, name, length, node) :
                                          -NARC_NOT_SUPPORTED;
        vnode_put(parent);
    }
    vnode_put(node);
    mutex_unlock(&namespace_lock);
    return status;
}

int vfs_lookup(struct vnode *base, const char *path, struct vnode **result) {
    if (!*path) return -NARC_NOT_FOUND;
    mutex_lock(&namespace_lock);
    int status = walk(base, path, strlen(path), result);
    mutex_unlock(&namespace_lock);
    return status;
}

static int create_locked(struct vnode *base, const char *path, uint32_t mode, uint32_t rdev,
                         struct vnode **result) {
    struct vnode *parent;
    const char *name;
    size_t length;
    int status = lookup_parent(base, path, &parent, &name, &length);
    if (status) return status;
    if (read_only(parent)) status = -NARC_READ_ONLY_FS;
    else status = parent->ops->create ? parent->ops->create(parent, name, length, mode, rdev, result) :
                                        -NARC_NOT_SUPPORTED;
    vnode_put(parent);
    return status;
}

static int open_locked(struct vnode *base, const char *path, uint32_t flags, uint32_t mode,
                       struct vnode **result) {
    int status = walk(base, path, strlen(path), result);
    if (status == -NARC_NOT_FOUND && (flags & VFS_OPEN_CREATE))
        return create_locked(base, path, S_IFREG | (mode & 07777), 0, result);
    if (status) return status;
    if ((flags & VFS_OPEN_CREATE) && (flags & VFS_OPEN_EXCLUSIVE)) status = -NARC_EXISTS;
    else if ((flags & VFS_OPEN_DIRECTORY) && (*result)->type != VFS_DIR)
        status = -NARC_NOT_DIRECTORY;
    else if ((flags & VFS_OPEN_WRITE) && (*result)->type == VFS_DIR)
        status = -NARC_IS_DIRECTORY;
    else if ((flags & VFS_OPEN_WRITE) && (*result)->type == VFS_REG && read_only(*result))
        status = -NARC_READ_ONLY_FS;
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
    int status = create_locked(base, path, S_IFDIR | (mode & 07777), 0, 0);
    mutex_unlock(&namespace_lock);
    return status;
}

int vfs_mknod(struct vnode *base, const char *path, uint32_t mode, uint32_t rdev) {
    mutex_lock(&namespace_lock);
    int status = create_locked(base, path, mode, rdev, 0);
    mutex_unlock(&namespace_lock);
    return status;
}

int vfs_unlink(struct vnode *base, const char *path, int directory) {
    struct vnode *parent;
    const char *name;
    size_t length;
    mutex_lock(&namespace_lock);
    int status = lookup_parent(base, path, &parent, &name, &length);
    if (!status && read_only(parent)) {
        vnode_put(parent);
        status = -NARC_READ_ONLY_FS;
    } else if (!status) {
        struct vnode *victim;
        if (parent->ops->lookup(parent, name, length, &victim) == 0) {
            if (victim->mounted || (victim == victim->mount->root)) status = -NARC_BUSY;
            vnode_put(victim);
        }
        if (!status)
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
    if (!status && read_only(old_parent)) status = -NARC_READ_ONLY_FS;
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
        node = cross_up(node);
        if (node == root) break;
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
    if (read_only(node)) return -NARC_READ_ONLY_FS;
    if (node->type != VFS_REG || !node->ops->write) return -NARC_INVALID_ARGUMENT;
    mutex_lock(&node->lock);
    long result = node->ops->write(node, offset, buffer, length);
    mutex_unlock(&node->lock);
    return result;
}

int vfs_truncate(struct vnode *node, uint64_t size) {
    if (node->type == VFS_DIR) return -NARC_IS_DIRECTORY;
    if (read_only(node)) return -NARC_READ_ONLY_FS;
    if (node->type != VFS_REG || !node->ops->truncate) return -NARC_INVALID_ARGUMENT;
    mutex_lock(&node->lock);
    int status = node->ops->truncate(node, size);
    mutex_unlock(&node->lock);
    return status;
}

int vfs_setattr(struct vnode *node, const struct vfs_attr *attr) {
    if (read_only(node)) return -NARC_READ_ONLY_FS;
    if (attr->mask & VFS_ATTR_SIZE) {
        int status = vfs_truncate(node, attr->size);
        if (status) return status;
    }
    if (!(attr->mask & ~VFS_ATTR_SIZE)) return 0;
    int64_t now = time_realtime_ns() / (int64_t)NSEC_PER_SEC;
    mutex_lock(&node->lock);
    if (attr->mask & VFS_ATTR_MODE) node->mode = (node->mode & S_IFMT) | (attr->mode & 07777);
    if ((attr->mask & (VFS_ATTR_UID | VFS_ATTR_GID)) && !(attr->mask & VFS_ATTR_MODE) &&
        node->type == VFS_REG)
        node->mode &= ~(uint32_t)06000;
    if (attr->mask & VFS_ATTR_UID) node->uid = attr->uid;
    if (attr->mask & VFS_ATTR_GID) node->gid = attr->gid;
    if (attr->mask & VFS_ATTR_ATIME) node->atime = attr->atime;
    if (attr->mask & VFS_ATTR_ATIME_NOW) node->atime = now;
    if (attr->mask & VFS_ATTR_MTIME) node->mtime = attr->mtime;
    if (attr->mask & VFS_ATTR_MTIME_NOW) node->mtime = now;
    node->ctime = now;
    int status = node->ops->write_attr ? node->ops->write_attr(node) : 0;
    mutex_unlock(&node->lock);
    return status;
}

int vfs_statfs(struct vnode *node, struct vfs_statfs *info) {
    memset(info, 0, sizeof(*info));
    info->block_size = PAGE_SIZE;
    info->name_max = VFS_NAME_MAX;
    struct mount *mount = node->mount;
    if (!mount || !mount->fs->statfs) return 0;
    return mount->fs->statfs(mount, info);
}

int vfs_readdir(struct vnode *dir, uint64_t *cookie, struct vfs_dirent *entry) {
    if (dir->type != VFS_DIR) return -NARC_NOT_DIRECTORY;
    mutex_lock(&namespace_lock);
    int result = dir->ops->readdir(dir, cookie, entry);
    mutex_unlock(&namespace_lock);
    return result;
}

static int mount_locked(struct vnode *base, const char *source, const char *target,
                        const char *type, uint32_t flags) {
    const struct filesystem *fs = find_filesystem(type);
    if (!fs) return -NARC_NO_DEVICE;
    struct vnode *point, *device = 0;
    int status = walk(base, target, strlen(target), &point);
    if (status) return status;
    if (point->type != VFS_DIR) status = -NARC_NOT_DIRECTORY;
    else if (point->mounted || (point == point->mount->root && point != root)) status = -NARC_BUSY;
    if (!status && fs->requires_device) {
        status = source ? walk(base, source, strlen(source), &device) : -NARC_NO_DEVICE;
        if (!status && device->type != VFS_BLK) status = -NARC_NO_DEVICE;
    }
    struct mount *mount = status ? 0 : vfs_mount_alloc(fs);
    if (!status && !mount) status = -NARC_NO_MEMORY;
    if (!status) {
        mount->flags = flags;
        status = fs->mount(mount, device);
    }
    vnode_put(device);
    if (status) {
        kfree(mount);
        vnode_put(point);
        return status;
    }
    mount->covered = point;
    point->mounted = mount;
    mount->next = mounts;
    mounts = mount;
    return 0;
}

int vfs_mount(struct vnode *base, const char *source, const char *target, const char *type,
              uint32_t flags) {
    mutex_lock(&namespace_lock);
    int status = mount_locked(base, source, target, type, flags);
    mutex_unlock(&namespace_lock);
    return status;
}

static int umount_locked(struct vnode *base, const char *target) {
    struct vnode *node;
    int status = walk(base, target, strlen(target), &node);
    if (status) return status;
    struct mount *mount = node->mount;
    if (node != mount->root || !mount->covered) status = -NARC_INVALID_ARGUMENT;
    else if (node->refs > 2 || node->mounted) status = -NARC_BUSY;
    for (struct mount *other = mounts; !status && other; other = other->next)
        if (other != mount && other->covered && other->covered->mount == mount) status = -NARC_BUSY;
    vnode_put(node);
    if (status) return status;
    if (mount->fs->unmount && (status = mount->fs->unmount(mount)) != 0) return status;
    struct mount **link = &mounts;
    while (*link != mount) link = &(*link)->next;
    *link = mount->next;
    mount->covered->mounted = 0;
    vnode_put(mount->covered);
    vnode_put(mount->root);
    kfree(mount);
    return 0;
}

int vfs_umount(struct vnode *base, const char *target) {
    mutex_lock(&namespace_lock);
    int status = umount_locked(base, target);
    mutex_unlock(&namespace_lock);
    return status;
}

int vfs_init(const void *archive, uint64_t size) {
    vfs_register(&tmpfs_filesystem);
    vfs_register(&ext2_filesystem);
    root = tmpfs_create_root();
    if (!root) return -1;
    return initramfs_unpack(root, archive, size) == 0 ? 0 : -1;
}
