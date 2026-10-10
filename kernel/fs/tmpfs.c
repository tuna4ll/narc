#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <narc/abi.h>

#define TREE_SHIFT 9
#define TREE_FANOUT (1u << TREE_SHIFT)
#define TREE_DEPTH_MAX 6

struct page_tree {
    uint64_t root;
    unsigned depth;
};

struct tmpfs_node;

struct tmpfs_dirent {
    struct tmpfs_dirent *next;
    struct tmpfs_node *node;
    size_t length;
    char name[];
};

struct tmpfs_node {
    struct vnode vnode;
    struct tmpfs_node *parent;
    struct tmpfs_dirent *entries;
    const uint8_t *archive;
    struct page_tree pages;
};

static const struct vnode_ops tmpfs_ops;
static struct mount tmpfs_mount;
static uint64_t next_inode = 1;

static struct tmpfs_node *tnode(struct vnode *node) {
    return (struct tmpfs_node *)node;
}

static uint64_t *tree_slot(struct page_tree *tree, uint64_t index, int create) {
    while (!tree->depth ||
           (tree->depth < TREE_DEPTH_MAX && (index >> (TREE_SHIFT * tree->depth)))) {
        if (!create) return 0;
        uint64_t root = pmm_alloc_page();
        if (!root) return 0;
        *(uint64_t *)phys_to_virt(root) = tree->root;
        tree->root = root;
        tree->depth++;
    }
    if (index >> (TREE_SHIFT * tree->depth)) return 0;
    uint64_t *table = phys_to_virt(tree->root);
    for (unsigned level = tree->depth; level > 1; level--) {
        uint64_t *entry = &table[(index >> (TREE_SHIFT * (level - 1))) & (TREE_FANOUT - 1)];
        if (!*entry) {
            if (!create || !(*entry = pmm_alloc_page())) return 0;
        }
        table = phys_to_virt(*entry);
    }
    return &table[index & (TREE_FANOUT - 1)];
}

static void tree_free(uint64_t table, unsigned level) {
    uint64_t *entries = phys_to_virt(table);
    for (size_t i = 0; i < TREE_FANOUT; i++) {
        if (!entries[i]) continue;
        if (level > 1) tree_free(entries[i], level - 1);
        else pmm_free_page(entries[i]);
    }
    pmm_free_page(table);
}

static void release_pages(struct tmpfs_node *node) {
    if (node->pages.root) tree_free(node->pages.root, node->pages.depth);
    node->pages.root = 0;
    node->pages.depth = 0;
}

static uint8_t *file_page(struct tmpfs_node *node, uint64_t index, int create) {
    uint64_t *slot = tree_slot(&node->pages, index, create);
    if (!slot) return 0;
    if (!*slot && (!create || !(*slot = pmm_alloc_page()))) return 0;
    return phys_to_virt(*slot);
}

static int detach_archive(struct tmpfs_node *node) {
    if (!node->archive) return 0;
    for (uint64_t offset = 0; offset < node->vnode.size; offset += PAGE_SIZE) {
        uint8_t *page = file_page(node, offset / PAGE_SIZE, 1);
        if (!page) return -NARC_NO_SPACE;
        size_t amount = node->vnode.size - offset > PAGE_SIZE ? PAGE_SIZE :
                        (size_t)(node->vnode.size - offset);
        memcpy(page, node->archive + offset, amount);
    }
    node->archive = 0;
    return 0;
}

static struct tmpfs_node *new_node(uint32_t mode, uint32_t rdev) {
    struct tmpfs_node *node = kzalloc(sizeof(*node));
    if (!node) return 0;
    vnode_init(&node->vnode, &tmpfs_mount, &tmpfs_ops, mode);
    node->vnode.ino = next_inode++;
    node->vnode.rdev = rdev;
    return node;
}

static void free_node(struct tmpfs_node *node) {
    release_pages(node);
    kfree(node);
}

static int name_eq(const struct tmpfs_dirent *entry, const char *name, size_t length) {
    return entry->length == length && !memcmp(entry->name, name, length);
}

static struct tmpfs_dirent **find_entry(struct tmpfs_node *dir, const char *name, size_t length) {
    struct tmpfs_dirent **link = &dir->entries;
    while (*link && !name_eq(*link, name, length)) link = &(*link)->next;
    return link;
}

static struct tmpfs_dirent *new_entry(struct tmpfs_node *node, const char *name, size_t length) {
    struct tmpfs_dirent *entry = kmalloc(sizeof(*entry) + length);
    if (!entry) return 0;
    entry->next = 0;
    entry->node = node;
    entry->length = length;
    memcpy(entry->name, name, length);
    return entry;
}

static void append_entry(struct tmpfs_node *dir, struct tmpfs_dirent *entry) {
    struct tmpfs_dirent **link = &dir->entries;
    while (*link) link = &(*link)->next;
    *link = entry;
}

static void drop_link(struct tmpfs_node *node) {
    if (node->vnode.type == VFS_DIR) {
        node->vnode.nlink = 0;
        if (node->parent) node->parent->vnode.nlink--;
        node->parent = 0;
    } else if (node->vnode.nlink) {
        node->vnode.nlink--;
    }
    if (!node->vnode.nlink && !node->vnode.refs) free_node(node);
}

static int tmpfs_lookup(struct vnode *dir, const char *name, size_t length,
                        struct vnode **result) {
    struct tmpfs_node *node = tnode(dir);
    if (!dir->nlink) return -NARC_NOT_FOUND;
    if (length == 2 && name[0] == '.' && name[1] == '.') {
        *result = vnode_get(node->parent ? &node->parent->vnode : dir);
        return 0;
    }
    struct tmpfs_dirent *entry = *find_entry(node, name, length);
    if (!entry) return -NARC_NOT_FOUND;
    *result = vnode_get(&entry->node->vnode);
    return 0;
}

static int tmpfs_create(struct vnode *dir, const char *name, size_t length, uint32_t mode,
                        uint32_t rdev, struct vnode **result) {
    struct tmpfs_node *parent = tnode(dir);
    if (!dir->nlink) return -NARC_NOT_FOUND;
    if (*find_entry(parent, name, length)) return -NARC_EXISTS;
    struct tmpfs_node *node = new_node(mode, rdev);
    struct tmpfs_dirent *entry = node ? new_entry(node, name, length) : 0;
    if (!entry) {
        kfree(node);
        return -NARC_NO_SPACE;
    }
    if (node->vnode.type == VFS_DIR) {
        node->parent = parent;
        node->vnode.nlink = 2;
        dir->nlink++;
    }
    append_entry(parent, entry);
    if (result) *result = vnode_get(&node->vnode);
    return 0;
}

static int tmpfs_unlink(struct vnode *dir, const char *name, size_t length, int directory) {
    struct tmpfs_dirent **link = find_entry(tnode(dir), name, length);
    struct tmpfs_dirent *entry = *link;
    if (!entry) return -NARC_NOT_FOUND;
    struct tmpfs_node *node = entry->node;
    if (directory && node->vnode.type != VFS_DIR) return -NARC_NOT_DIRECTORY;
    if (!directory && node->vnode.type == VFS_DIR) return -NARC_IS_DIRECTORY;
    if (node->entries) return -NARC_NOT_EMPTY;
    *link = entry->next;
    kfree(entry);
    drop_link(node);
    return 0;
}

static int tmpfs_rename(struct vnode *old_dir, const char *old_name, size_t old_length,
                        struct vnode *new_dir, const char *new_name, size_t new_length) {
    struct tmpfs_node *from = tnode(old_dir), *to = tnode(new_dir);
    struct tmpfs_dirent **source_link = find_entry(from, old_name, old_length);
    struct tmpfs_dirent *source = *source_link;
    if (!source) return -NARC_NOT_FOUND;
    struct tmpfs_node *node = source->node;
    struct tmpfs_dirent **target_link = find_entry(to, new_name, new_length);
    struct tmpfs_dirent *target = *target_link;
    if (target && target->node == node) return 0;
    if (target) {
        struct tmpfs_node *victim = target->node;
        if (node->vnode.type == VFS_DIR && victim->vnode.type != VFS_DIR)
            return -NARC_NOT_DIRECTORY;
        if (node->vnode.type != VFS_DIR && victim->vnode.type == VFS_DIR)
            return -NARC_IS_DIRECTORY;
        if (victim->entries) return -NARC_NOT_EMPTY;
    }
    struct tmpfs_dirent *moved = new_entry(node, new_name, new_length);
    if (!moved) return -NARC_NO_SPACE;
    if (target) {
        *target_link = target->next;
        drop_link(target->node);
        kfree(target);
    }
    source_link = find_entry(from, old_name, old_length);
    source = *source_link;
    *source_link = source->next;
    kfree(source);
    append_entry(to, moved);
    if (node->vnode.type == VFS_DIR && from != to) {
        from->vnode.nlink--;
        to->vnode.nlink++;
        node->parent = to;
    }
    return 0;
}

static int tmpfs_readdir(struct vnode *dir, uint64_t *cookie, struct vfs_dirent *entry) {
    struct tmpfs_node *node = tnode(dir);
    if (*cookie < 2) {
        struct tmpfs_node *target = *cookie == 0 || !node->parent ? node : node->parent;
        entry->ino = target->vnode.ino;
        entry->type = VFS_DIR;
        entry->name[0] = '.';
        entry->name[1] = *cookie ? '.' : 0;
        entry->name[2] = 0;
        (*cookie)++;
        return 1;
    }
    struct tmpfs_dirent *child = node->entries;
    for (uint64_t i = 2; child && i < *cookie; i++) child = child->next;
    if (!child) return 0;
    entry->ino = child->node->vnode.ino;
    entry->type = child->node->vnode.type;
    memcpy(entry->name, child->name, child->length);
    entry->name[child->length] = 0;
    (*cookie)++;
    return 1;
}

static long tmpfs_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length) {
    struct tmpfs_node *node = tnode(vnode);
    if (offset >= vnode->size) return 0;
    uint64_t available = vnode->size - offset;
    if ((uint64_t)length > available) length = (size_t)available;
    size_t done = 0;
    while (done < length) {
        uint64_t position = offset + done;
        size_t in_page = (size_t)(position & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - in_page;
        if (chunk > length - done) chunk = length - done;
        const uint8_t *source = node->archive ? node->archive + position :
                                file_page(node, position / PAGE_SIZE, 0);
        if (!source) memset((uint8_t *)buffer + done, 0, chunk);
        else memcpy((uint8_t *)buffer + done, source + (node->archive ? 0 : in_page), chunk);
        done += chunk;
    }
    return (long)done;
}

static long tmpfs_write(struct vnode *vnode, uint64_t offset, const void *buffer,
                        size_t length) {
    struct tmpfs_node *node = tnode(vnode);
    if ((uint64_t)length > (uint64_t)INT64_MAX - offset) return -NARC_INVALID_ARGUMENT;
    int status = detach_archive(node);
    if (status) return status;
    size_t done = 0;
    while (done < length) {
        uint64_t position = offset + done;
        size_t in_page = (size_t)(position & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - in_page;
        if (chunk > length - done) chunk = length - done;
        uint8_t *page = file_page(node, position / PAGE_SIZE, 1);
        if (!page) break;
        memcpy(page + in_page, (const uint8_t *)buffer + done, chunk);
        done += chunk;
    }
    if (!done && length) return -NARC_NO_SPACE;
    if (offset + done > vnode->size) vnode->size = offset + done;
    return (long)done;
}

static int tmpfs_truncate(struct vnode *vnode, uint64_t size) {
    struct tmpfs_node *node = tnode(vnode);
    if (!size) {
        release_pages(node);
        node->archive = 0;
    } else if (size < vnode->size) {
        int status = detach_archive(node);
        if (status) return status;
        uint64_t first = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t last = (vnode->size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t index = first; index < last; index++) {
            uint64_t *slot = tree_slot(&node->pages, index, 0);
            if (!slot || !*slot) continue;
            pmm_free_page(*slot);
            *slot = 0;
        }
        uint8_t *tail = size & (PAGE_SIZE - 1) ? file_page(node, size / PAGE_SIZE, 0) : 0;
        if (tail) memset(tail + (size & (PAGE_SIZE - 1)), 0, PAGE_SIZE - (size & (PAGE_SIZE - 1)));
    }
    vnode->size = size;
    return 0;
}

static void tmpfs_release(struct vnode *vnode) {
    if (!vnode->nlink) free_node(tnode(vnode));
}

static const struct vnode_ops tmpfs_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .unlink = tmpfs_unlink,
    .rename = tmpfs_rename,
    .readdir = tmpfs_readdir,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .truncate = tmpfs_truncate,
    .release = tmpfs_release,
};

struct vnode *tmpfs_create_root(void) {
    struct tmpfs_node *root = new_node(S_IFDIR | 0755, 0);
    if (!root) return 0;
    root->vnode.nlink = 2;
    root->vnode.refs = 1;
    tmpfs_mount.root = &root->vnode;
    return &root->vnode;
}

int tmpfs_attach_archive(struct vnode *vnode, const void *data, uint64_t size) {
    if (vnode->ops != &tmpfs_ops || vnode->type != VFS_REG) return -NARC_INVALID_ARGUMENT;
    struct tmpfs_node *node = tnode(vnode);
    release_pages(node);
    node->archive = data;
    vnode->size = size;
    return 0;
}
