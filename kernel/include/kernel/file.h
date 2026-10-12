#pragma once
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

struct open_file;

struct file_ops {
    long (*read)(struct open_file *open, void *buffer, size_t length);
    long (*write)(struct open_file *open, const void *buffer, size_t length);
    long (*ioctl)(struct open_file *open, uint32_t request, uint64_t argument);
    void (*release)(struct open_file *open);
};

struct open_file {
    uint32_t refs;
    uint32_t flags;
    const struct file_ops *ops;
    void *private;
    struct vnode *node;
    uint64_t offset;
    struct mutex lock;
};

#define FD_CLOEXEC 1u
#define FD_LIMIT 1024

struct fd_entry {
    struct open_file *file;
    uint32_t flags;
};

struct fd_table {
    struct spinlock lock;
    struct fd_entry *entries;
    int count;
};

struct open_file *file_alloc(const struct file_ops *ops, void *private);
int file_open(struct vnode *base, const char *path, uint32_t flags, uint32_t mode,
              struct open_file **result);
struct open_file *file_get(struct open_file *open);
void file_put(struct open_file *open);
long file_read(struct open_file *open, void *buffer, size_t length);
long file_write(struct open_file *open, const void *buffer, size_t length);
long file_seek(struct open_file *open, int64_t offset, int origin);
long file_ioctl(struct open_file *open, uint32_t request, uint64_t argument);
int file_readdir(struct open_file *open, struct vfs_dirent *entry);

struct fd_table *fd_table_create(void);
struct fd_table *fd_table_clone(struct fd_table *table);
void fd_table_release(struct fd_table *table);
void fd_table_exec(struct fd_table *table);
int fd_install(struct fd_table *table, struct open_file *open, int minimum, uint32_t flags);
struct open_file *fd_get(struct fd_table *table, int fd);
int fd_close(struct fd_table *table, int fd);
int fd_dup(struct fd_table *table, int fd, int minimum, uint32_t flags);
int fd_dup2(struct fd_table *table, int oldfd, int newfd);
int fd_get_flags(struct fd_table *table, int fd);
int fd_set_flags(struct fd_table *table, int fd, uint32_t flags);
