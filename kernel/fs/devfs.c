#include <stdint.h>

#include "devfs.h"
#include "keyboard.h"
#include "terminal.h"

#define DEVFS_NODES 2

static struct vnode nodes[DEVFS_NODES];

static int64_t console_read(struct vnode *node, uint64_t offset, char *buf, uint64_t len) {
    (void)node;
    (void)offset;

    uint64_t n = 0;
    while (n < len && keyboard_read_char(&buf[n]))
        n++;

    if (n == 0 && len > 0)
        return VFS_AGAIN;
    return n;
}

static int64_t console_write(struct vnode *node, uint64_t offset, const char *buf, uint64_t len) {
    (void)node;
    (void)offset;

    for (uint64_t i = 0; i < len; i++)
        terminal_putchar(buf[i]);
    return len;
}

static struct vnode *devfs_lookup(struct vnode *dir, const char *name) {
    return vfs_table_lookup(nodes, DEVFS_NODES, dir, name);
}

static struct vnode *devfs_readdir(struct vnode *dir, uint64_t index) {
    return vfs_table_child(nodes, DEVFS_NODES, dir, index);
}

static const struct vnode_ops dir_ops = {
    .lookup = devfs_lookup,
    .readdir = devfs_readdir,
};

static const struct vnode_ops console_ops = {
    .read = console_read,
    .write = console_write,
};

static struct vnode nodes[DEVFS_NODES] = {
    { .type = VFS_DIR, .ops = &dir_ops },
    { .name = "console", .type = VFS_FILE, .ops = &console_ops, .parent = &nodes[0] },
};

struct vnode *devfs_root(void) {
    return &nodes[0];
}
