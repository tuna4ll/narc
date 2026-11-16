#include <stdbool.h>
#include <stdint.h>

#include "initramfs.h"
#include "mem.h"
#include "heap.h"

#define TAR_BLOCK 512

struct tar_header {
    char name[INITRAMFS_NAME_LEN];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
};

static const uint8_t *tar_base = NULL;
static uint64_t tar_size = 0;

static uint64_t parse_octal(const char *s, int n) {
    uint64_t value = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        value = value * 8 + (s[i] - '0');
    return value;
}

void initramfs_init(const uint8_t *base, uint64_t size) {
    tar_base = base;
    tar_size = size;
}

static struct vnode *nodes = NULL;
static uint64_t node_count = 0;

// name may fill all 100 bytes without a nul
static bool initramfs_entry(uint64_t index, const char **name, bool *is_dir, const uint8_t **data, uint64_t *size) {
    uint64_t off = 0;
    uint64_t n = 0;

    while (tar_size >= TAR_BLOCK && off <= tar_size - TAR_BLOCK) {
        const struct tar_header *h = (const struct tar_header *)(tar_base + off);
        if (memcmp(h->magic, "ustar", 5) != 0)
            return false;

        uint64_t file_size = parse_octal(h->size, sizeof(h->size));
        if (file_size > tar_size - off - TAR_BLOCK)
            return false;

        bool file = h->typeflag == '0' || h->typeflag == '\0';
        if ((file || h->typeflag == '5') && n++ == index) {
            *name = h->name;
            *is_dir = !file;
            *data = tar_base + off + TAR_BLOCK;
            *size = file_size;
            return true;
        }

        off += TAR_BLOCK + ((file_size + TAR_BLOCK - 1) & ~(uint64_t)(TAR_BLOCK - 1));
    }
    return false;
}

static uint64_t name_length(const char *name) {
    uint64_t len = 0;
    while (len < INITRAMFS_NAME_LEN && name[len] != '\0')
        len++;
    return len;
}

static int64_t initramfs_read(struct vnode *node, uint64_t offset, char *buf, uint64_t len) {
    if (offset >= node->size)
        return 0;

    uint64_t n = len < node->size - offset ? len : node->size - offset;
    memcpy(buf, (const uint8_t *)node->data + offset, n);
    return n;
}

static struct vnode *initramfs_lookup(struct vnode *dir, const char *name) {
    return vfs_table_lookup(nodes, node_count, dir, name);
}

static struct vnode *initramfs_readdir(struct vnode *dir, uint64_t index) {
    return vfs_table_child(nodes, node_count, dir, index);
}

static const struct vnode_ops ops = {
    .read = initramfs_read,
    .lookup = initramfs_lookup,
    .readdir = initramfs_readdir,
};

// walks the path and makes every dir that is missing on the way
static void add_entry(const char *path, uint64_t path_len, bool is_dir, const uint8_t *data, uint64_t size) {
    struct vnode *dir = &nodes[0];
    uint64_t i = 0;

    while (i < path_len) {
        while (i < path_len && path[i] == '/')
            i++;
        uint64_t start = i;
        while (i < path_len && path[i] != '/')
            i++;

        uint64_t len = i - start;
        if (len == 0 || (len == 1 && path[start] == '.'))
            continue;
        if (len >= VFS_NAME_LEN || (len == 2 && path[start] == '.' && path[start + 1] == '.'))
            return;

        char name[VFS_NAME_LEN];
        memcpy(name, path + start, len);
        name[len] = '\0';

        uint64_t rest = i;
        while (rest < path_len && path[rest] == '/')
            rest++;
        bool last = rest == path_len;

        struct vnode *node = vfs_table_lookup(nodes, node_count, dir, name);
        if (node == NULL) {
            node = &nodes[node_count++];
            memcpy(node->name, name, len + 1);
            node->type = last && !is_dir ? VFS_FILE : VFS_DIR;
            node->ops = &ops;
            node->parent = dir;
            if (node->type == VFS_FILE) {
                node->data = data;
                node->size = size;
            }
        }

        if (node->type != VFS_DIR)
            return;
        dir = node;
    }
}

struct vnode *initramfs_root(void) {
    const char *name;
    bool is_dir;
    const uint8_t *data;
    uint64_t size;

    // every path part may need its own node plus one for the root
    uint64_t capacity = 1;
    for (uint64_t i = 0; initramfs_entry(i, &name, &is_dir, &data, &size); i++) {
        uint64_t len = name_length(name);
        capacity++;
        for (uint64_t j = 0; j < len; j++)
            if (name[j] == '/')
                capacity++;
    }

    nodes = kcalloc(capacity, sizeof(struct vnode));
    if (nodes == NULL)
        return NULL;

    nodes[0].type = VFS_DIR;
    nodes[0].ops = &ops;
    node_count = 1;

    for (uint64_t i = 0; initramfs_entry(i, &name, &is_dir, &data, &size); i++)
        add_entry(name, name_length(name), is_dir, data, size);

    return &nodes[0];
}
