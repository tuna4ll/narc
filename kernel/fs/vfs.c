#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vfs.h"
#include "mem.h"

#define MAX_MOUNTS 4

struct mount {
    // normalized and the root is the empty string
    char path[VFS_PATH_MAX];
    uint64_t len;
    struct vnode *root;
};

static struct mount mounts[MAX_MOUNTS];
static int mount_count = 0;

// turns a//b/./c/../d into /a/b/d and the root into the empty string
static bool normalize(const char *in, char *out, uint64_t *out_len) {
    uint64_t n = 0;

    while (*in != '\0') {
        while (*in == '/')
            in++;
        if (*in == '\0')
            break;

        const char *end = in;
        while (*end != '\0' && *end != '/')
            end++;
        uint64_t len = end - in;

        if (len == 2 && in[0] == '.' && in[1] == '.') {
            while (n > 0 && out[--n] != '/') {
            }
        } else if (len != 1 || in[0] != '.') {
            if (n + 1 + len >= VFS_PATH_MAX)
                return false;
            out[n++] = '/';
            memcpy(out + n, in, len);
            n += len;
        }
        in = end;
    }

    out[n] = '\0';
    *out_len = n;
    return true;
}

bool vfs_normalize(const char *path, char *out) {
    uint64_t len;
    if (!normalize(path, out, &len))
        return false;

    if (len == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    return true;
}

static struct vnode *lookup_in(struct vnode *dir, const char *name) {
    if (dir->type != VFS_DIR || dir->ops == NULL || dir->ops->lookup == NULL)
        return NULL;
    return dir->ops->lookup(dir, name);
}

bool vfs_mount(const char *path, struct vnode *root) {
    if (root == NULL || mount_count == MAX_MOUNTS)
        return false;

    struct mount *m = &mounts[mount_count];
    if (!normalize(path, m->path, &m->len))
        return false;

    // a mounted root is listed under the name of its mount point
    const char *last = m->path;
    for (uint64_t i = 0; i < m->len; i++)
        if (m->path[i] == '/')
            last = m->path + i + 1;
    uint64_t name_len = m->path + m->len - last;
    if (name_len >= VFS_NAME_LEN)
        return false;
    memcpy(root->name, last, name_len + 1);

    m->root = root;
    mount_count++;
    return true;
}

struct vnode *vfs_lookup(const char *path) {
    char norm[VFS_PATH_MAX];
    uint64_t len;
    if (!normalize(path, norm, &len))
        return NULL;

    // the longest mount that covers the path wins
    struct mount *best = NULL;
    for (int i = 0; i < mount_count; i++) {
        struct mount *m = &mounts[i];
        bool covers = m->len <= len && memcmp(norm, m->path, m->len) == 0
            && (norm[m->len] == '\0' || norm[m->len] == '/');
        if (covers && (best == NULL || m->len > best->len))
            best = m;
    }
    if (best == NULL)
        return NULL;

    struct vnode *node = best->root;
    const char *p = norm + best->len;

    while (*p == '/' && node != NULL) {
        p++;
        const char *end = p;
        while (*end != '\0' && *end != '/')
            end++;

        char name[VFS_NAME_LEN];
        uint64_t name_len = end - p;
        if (name_len >= VFS_NAME_LEN)
            return NULL;
        memcpy(name, p, name_len);
        name[name_len] = '\0';

        node = lookup_in(node, name);
        p = end;
    }
    return node;
}

struct vnode *vfs_child(struct vnode *dir, uint64_t index) {
    if (dir->type != VFS_DIR || dir->ops == NULL || dir->ops->readdir == NULL)
        return NULL;

    uint64_t count = 0;
    struct vnode *child;
    while ((child = dir->ops->readdir(dir, count)) != NULL) {
        if (count == index)
            return child;
        count++;
    }

    // mount points show up in the dir that holds them
    for (int i = 0; i < mount_count; i++) {
        struct mount *m = &mounts[i];
        if (m->len == 0 || lookup_in(dir, m->root->name) != NULL)
            continue;

        char parent[VFS_PATH_MAX];
        uint64_t slash = m->len - 1;
        while (m->path[slash] != '/')
            slash--;
        memcpy(parent, m->path, slash);
        parent[slash] = '\0';

        if (vfs_lookup(parent) == dir && count++ == index)
            return m->root;
    }
    return NULL;
}

int64_t vfs_read(struct vnode *node, uint64_t offset, char *buf, uint64_t len) {
    if (node->type != VFS_FILE || node->ops == NULL || node->ops->read == NULL)
        return -1;
    return node->ops->read(node, offset, buf, len);
}

int64_t vfs_write(struct vnode *node, uint64_t offset, const char *buf, uint64_t len) {
    if (node->type != VFS_FILE || node->ops == NULL || node->ops->write == NULL)
        return -1;
    return node->ops->write(node, offset, buf, len);
}

struct vnode *vfs_table_lookup(struct vnode *nodes, uint64_t count, struct vnode *dir, const char *name) {
    for (uint64_t i = 0; i < count; i++)
        if (nodes[i].parent == dir && strcmp(nodes[i].name, name) == 0)
            return &nodes[i];
    return NULL;
}

struct vnode *vfs_table_child(struct vnode *nodes, uint64_t count, struct vnode *dir, uint64_t index) {
    for (uint64_t i = 0; i < count; i++)
        if (nodes[i].parent == dir && index-- == 0)
            return &nodes[i];
    return NULL;
}
