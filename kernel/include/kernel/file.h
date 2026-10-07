#pragma once
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

struct open_file;

struct file_ops {
    long (*read)(struct open_file *open, void *buffer, size_t length);
    long (*write)(struct open_file *open, const void *buffer, size_t length);
    void (*release)(struct open_file *open);
};

struct open_file {
    uint32_t refs;
    const struct file_ops *ops;
    void *private;
    struct file file;
};

struct fd_table {
    struct open_file **files;
    int count;
};

struct open_file *file_alloc(const struct file_ops *ops, void *private);
struct open_file *file_open(const char *path, uint32_t flags, enum vfs_status *status);
struct open_file *file_get(struct open_file *open);
void file_put(struct open_file *open);
struct file *file_vfs(struct open_file *open);
long file_read(struct open_file *open, void *buffer, size_t length);
long file_write(struct open_file *open, const void *buffer, size_t length);

struct fd_table *fd_table_create(void);
struct fd_table *fd_table_clone(struct fd_table *table);
void fd_table_release(struct fd_table *table);
int fd_install(struct fd_table *table, struct open_file *open, int minimum);
struct open_file *fd_get(struct fd_table *table, int fd);
int fd_close(struct fd_table *table, int fd);
int fd_dup(struct fd_table *table, int fd, int minimum);
int fd_dup2(struct fd_table *table, int oldfd, int newfd);
