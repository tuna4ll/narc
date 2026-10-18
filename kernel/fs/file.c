#include <kernel/device.h>
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

int file_open(struct vnode *base, const char *path, uint32_t flags, uint32_t mode,
              struct open_file **result) {
    struct vnode *node;
    int status = vfs_open(base, path, flags, mode, &node);
    if (status) return status;
    if (node->type == VFS_CHR || node->type == VFS_BLK) {
        status = device_open(node, flags, result);
        vnode_put(node);
        return status;
    }
    struct open_file *open = file_alloc(&vnode_file_ops, 0);
    if (!open) {
        vnode_put(node);
        return -NARC_NO_MEMORY;
    }
    open->node = node;
    open->flags = flags & VFS_STATUS_FLAGS;
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

long file_ioctl(struct open_file *open, uint32_t request, uint64_t argument) {
    return open->ops->ioctl ? open->ops->ioctl(open, request, argument) : -NARC_NOT_A_TTY;
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
    clone->entries = kmalloc((size_t)table->count * sizeof(*clone->entries));
    if (clone->entries || !table->count) {
        clone->count = table->count;
        for (int fd = 0; fd < clone->count; fd++) {
            clone->entries[fd] = table->entries[fd];
            file_get(clone->entries[fd].file);
        }
    }
    spin_unlock(&table->lock);
    if (!clone->entries && table->count) {
        kfree(clone);
        return 0;
    }
    return clone;
}

void fd_table_release(struct fd_table *table) {
    if (!table) return;
    for (int fd = 0; fd < table->count; fd++) file_put(table->entries[fd].file);
    kfree(table->entries);
    kfree(table);
}

void fd_table_exec(struct fd_table *table) {
    for (int fd = 0; fd < table->count; fd++) {
        spin_lock(&table->lock);
        struct open_file *open = 0;
        if (table->entries[fd].flags & FD_CLOEXEC) {
            open = table->entries[fd].file;
            table->entries[fd] = (struct fd_entry) { 0 };
        }
        spin_unlock(&table->lock);
        file_put(open);
    }
}

static int reserve(struct fd_table *table, int fd) {
    if (fd < table->count) return 0;
    if (fd >= FD_LIMIT) return -1;
    int count = table->count ? table->count : 8;
    while (count <= fd) count *= 2;
    if (count > FD_LIMIT) count = FD_LIMIT;
    struct fd_entry *entries = krealloc(table->entries, (size_t)count * sizeof(*entries));
    if (!entries) return -1;
    memset(entries + table->count, 0, (size_t)(count - table->count) * sizeof(*entries));
    table->entries = entries;
    table->count = count;
    return 0;
}

static int install_locked(struct fd_table *table, struct open_file *open, int minimum,
                          uint32_t flags) {
    int fd = minimum;
    while (fd < table->count && table->entries[fd].file) fd++;
    if (reserve(table, fd) != 0) return -1;
    table->entries[fd] = (struct fd_entry) { open, flags };
    return fd;
}

int fd_install(struct fd_table *table, struct open_file *open, int minimum, uint32_t flags) {
    if (minimum < 0) return -1;
    spin_lock(&table->lock);
    int fd = install_locked(table, open, minimum, flags);
    spin_unlock(&table->lock);
    return fd;
}

static struct fd_entry *entry_locked(struct fd_table *table, int fd) {
    return fd >= 0 && fd < table->count && table->entries[fd].file ? &table->entries[fd] : 0;
}

struct open_file *fd_get(struct fd_table *table, int fd) {
    spin_lock(&table->lock);
    struct fd_entry *entry = entry_locked(table, fd);
    struct open_file *open = entry ? file_get(entry->file) : 0;
    spin_unlock(&table->lock);
    return open;
}

int fd_close(struct fd_table *table, int fd) {
    spin_lock(&table->lock);
    struct fd_entry *entry = entry_locked(table, fd);
    struct open_file *open = entry ? entry->file : 0;
    if (entry) *entry = (struct fd_entry) { 0 };
    spin_unlock(&table->lock);
    if (!open) return -1;
    file_put(open);
    return 0;
}

int fd_dup(struct fd_table *table, int fd, int minimum, uint32_t flags) {
    if (minimum < 0 || minimum >= FD_LIMIT) return -1;
    spin_lock(&table->lock);
    struct fd_entry *entry = entry_locked(table, fd);
    struct open_file *open = entry ? entry->file : 0;
    int result = open ? install_locked(table, open, minimum, flags) : -1;
    if (result >= 0) file_get(open);
    spin_unlock(&table->lock);
    return result;
}

int fd_dup2(struct fd_table *table, int oldfd, int newfd) {
    spin_lock(&table->lock);
    struct fd_entry *entry = entry_locked(table, oldfd);
    struct open_file *open = entry ? entry->file : 0;
    struct open_file *old = 0;
    int result = -1;
    if (open && newfd >= 0 && (oldfd == newfd || reserve(table, newfd) == 0)) {
        result = newfd;
        if (oldfd != newfd) {
            old = table->entries[newfd].file;
            table->entries[newfd] = (struct fd_entry) { file_get(open), 0 };
        }
    }
    spin_unlock(&table->lock);
    file_put(old);
    return result;
}

int fd_get_flags(struct fd_table *table, int fd) {
    spin_lock(&table->lock);
    struct fd_entry *entry = entry_locked(table, fd);
    int flags = entry ? (int)entry->flags : -1;
    spin_unlock(&table->lock);
    return flags;
}

int fd_set_flags(struct fd_table *table, int fd, uint32_t flags) {
    spin_lock(&table->lock);
    struct fd_entry *entry = entry_locked(table, fd);
    if (entry) entry->flags = flags & FD_CLOEXEC;
    spin_unlock(&table->lock);
    return entry ? 0 : -1;
}
