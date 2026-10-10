#include <kernel/file.h>
#include <kernel/heap.h>
#include <kernel/string.h>
#include <narc/abi.h>

static long vnode_file_read(struct open_file *open, void *buffer, size_t length) {
    mutex_lock(&open->lock);
    long result = vfs_read(open->node, open->offset, buffer, length);
    if (result > 0) open->offset += (uint64_t)result;
    mutex_unlock(&open->lock);
    return result;
}

static long vnode_file_write(struct open_file *open, const void *buffer, size_t length) {
    mutex_lock(&open->lock);
    if (open->flags & VFS_OPEN_APPEND) open->offset = open->node->size;
    long result = vfs_write(open->node, open->offset, buffer, length);
    if (result > 0) open->offset += (uint64_t)result;
    mutex_unlock(&open->lock);
    return result;
}

static const struct file_ops vnode_file_ops = {
    .read = vnode_file_read,
    .write = vnode_file_write,
};

struct open_file *file_alloc(const struct file_ops *ops, void *private) {
    struct open_file *open = kzalloc(sizeof(*open));
    if (!open) return 0;
    open->refs = 1;
    open->ops = ops;
    open->private = private;
    mutex_init(&open->lock);
    return open;
}

int file_open(const char *path, uint32_t flags, uint32_t mode, struct open_file **result) {
    struct vnode *node;
    int status = vfs_open(path, flags, mode, &node);
    if (status) return status;
    struct open_file *open = file_alloc(&vnode_file_ops, 0);
    if (!open) {
        vnode_put(node);
        return -NARC_NO_MEMORY;
    }
    open->node = node;
    open->flags = flags & (VFS_OPEN_READ | VFS_OPEN_WRITE | VFS_OPEN_APPEND);
    *result = open;
    return 0;
}

struct open_file *file_get(struct open_file *open) {
    if (open) __atomic_fetch_add(&open->refs, 1, __ATOMIC_RELAXED);
    return open;
}

void file_put(struct open_file *open) {
    if (!open || __atomic_sub_fetch(&open->refs, 1, __ATOMIC_ACQ_REL)) return;
    if (open->ops->release) open->ops->release(open);
    vnode_put(open->node);
    kfree(open);
}

long file_read(struct open_file *open, void *buffer, size_t length) {
    if (open->node && !(open->flags & VFS_OPEN_READ)) return -NARC_BAD_HANDLE;
    return open->ops->read ? open->ops->read(open, buffer, length) : -NARC_BAD_HANDLE;
}

long file_write(struct open_file *open, const void *buffer, size_t length) {
    if (open->node && !(open->flags & VFS_OPEN_WRITE)) return -NARC_BAD_HANDLE;
    return open->ops->write ? open->ops->write(open, buffer, length) : -NARC_BAD_HANDLE;
}

long file_seek(struct open_file *open, int64_t offset, int origin) {
    struct vnode *node = open->node;
    if (!node || (node->type != VFS_REG && node->type != VFS_DIR))
        return -NARC_ILLEGAL_SEEK;
    mutex_lock(&open->lock);
    int64_t base = origin == 0 ? 0 : origin == 1 ? (int64_t)open->offset :
                   origin == 2 ? (int64_t)node->size : -1;
    long result = -NARC_INVALID_ARGUMENT;
    if (base >= 0 && offset >= -base && !(offset > 0 && base > INT64_MAX - offset)) {
        open->offset = (uint64_t)(base + offset);
        result = (long)open->offset;
    }
    mutex_unlock(&open->lock);
    return result;
}

int file_readdir(struct open_file *open, struct vfs_dirent *entry) {
    if (!open->node) return -NARC_NOT_DIRECTORY;
    mutex_lock(&open->lock);
    int result = vfs_readdir(open->node, &open->offset, entry);
    mutex_unlock(&open->lock);
    return result;
}

struct fd_table *fd_table_create(void) {
    struct fd_table *table = kzalloc(sizeof(*table));
    if (table) spin_init(&table->lock);
    return table;
}

struct fd_table *fd_table_clone(struct fd_table *table) {
    struct fd_table *clone = fd_table_create();
    if (!clone) return 0;
    spin_lock(&table->lock);
    clone->files = kmalloc((size_t)table->count * sizeof(*clone->files));
    if (clone->files || !table->count) {
        clone->count = table->count;
        for (int fd = 0; fd < clone->count; fd++) clone->files[fd] = file_get(table->files[fd]);
    }
    spin_unlock(&table->lock);
    if (!clone->files && table->count) {
        kfree(clone);
        return 0;
    }
    return clone;
}

void fd_table_release(struct fd_table *table) {
    if (!table) return;
    for (int fd = 0; fd < table->count; fd++) file_put(table->files[fd]);
    kfree(table->files);
    kfree(table);
}

static int reserve(struct fd_table *table, int fd) {
    if (fd < table->count) return 0;
    if (fd == INT32_MAX) return -1;
    int count = table->count ? table->count : 8;
    while (count <= fd) count = count > INT32_MAX / 2 ? INT32_MAX : count * 2;
    struct open_file **files = krealloc(table->files, (size_t)count * sizeof(*files));
    if (!files) return -1;
    memset(files + table->count, 0, (size_t)(count - table->count) * sizeof(*files));
    table->files = files;
    table->count = count;
    return 0;
}

static int install_locked(struct fd_table *table, struct open_file *open, int minimum) {
    int fd = minimum;
    while (fd < table->count && table->files[fd]) fd++;
    if (reserve(table, fd) != 0) return -1;
    table->files[fd] = open;
    return fd;
}

int fd_install(struct fd_table *table, struct open_file *open, int minimum) {
    if (minimum < 0) return -1;
    spin_lock(&table->lock);
    int fd = install_locked(table, open, minimum);
    spin_unlock(&table->lock);
    return fd;
}

struct open_file *fd_get(struct fd_table *table, int fd) {
    spin_lock(&table->lock);
    struct open_file *open = fd >= 0 && fd < table->count ? file_get(table->files[fd]) : 0;
    spin_unlock(&table->lock);
    return open;
}

int fd_close(struct fd_table *table, int fd) {
    spin_lock(&table->lock);
    struct open_file *open = fd >= 0 && fd < table->count ? table->files[fd] : 0;
    if (open) table->files[fd] = 0;
    spin_unlock(&table->lock);
    if (!open) return -1;
    file_put(open);
    return 0;
}

int fd_dup(struct fd_table *table, int fd, int minimum) {
    if (minimum < 0) return -1;
    spin_lock(&table->lock);
    struct open_file *open = fd >= 0 && fd < table->count ? table->files[fd] : 0;
    int result = open ? install_locked(table, open, minimum) : -1;
    if (result >= 0) file_get(open);
    spin_unlock(&table->lock);
    return result;
}

int fd_dup2(struct fd_table *table, int oldfd, int newfd) {
    spin_lock(&table->lock);
    struct open_file *open = oldfd >= 0 && oldfd < table->count ? table->files[oldfd] : 0;
    struct open_file *old = 0;
    int result = -1;
    if (open && newfd >= 0 && (oldfd == newfd || reserve(table, newfd) == 0)) {
        result = newfd;
        if (oldfd != newfd) {
            old = table->files[newfd];
            table->files[newfd] = file_get(open);
        }
    }
    spin_unlock(&table->lock);
    file_put(old);
    return result;
}
