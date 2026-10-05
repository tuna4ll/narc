#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define TAR_BLOCK 512
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define TREE_SHIFT 9
#define TREE_FANOUT (1u << TREE_SHIFT)
#define TREE_DEPTH_MAX 6

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char link[100];
    char magic[6];
    char version[2];
    char owner[32];
    char group[32];
    char major[8];
    char minor[8];
    char prefix[155];
    char pad[12];
};

struct page_tree {
    uint64_t root;
    unsigned depth;
};

struct vnode {
    struct vnode *parent, *children, *next;
    char *name;
    int linked;
    uint32_t refs;
    uint64_t size;
    uint64_t ino;
    uint32_t mode;
    uint8_t type;
    const uint8_t *archive;
    struct page_tree pages;
};

static const uint8_t *tar_data;
static uint64_t tar_size;
static uint64_t next_inode;
static struct vnode *root;

static size_t str_len(const char *string) {
    size_t length = 0;
    while (string[length]) length++;
    return length;
}

static int name_eq(const char *name, const char *component, size_t length) {
    for (size_t i = 0; i < length; i++)
        if (name[i] != component[i]) return 0;
    return !name[length];
}

static int dot_name(const char *component, size_t length) {
    return (length == 1 && component[0] == '.') ||
           (length == 2 && component[0] == '.' && component[1] == '.');
}

static uint64_t octal(const char *string, size_t length) {
    uint64_t value = 0;
    for (size_t i = 0; i < length && string[i]; i++) {
        if (string[i] < '0' || string[i] > '7') continue;
        value = (value << 3) + (uint64_t)(string[i] - '0');
    }
    return value;
}

static int zero_block(const uint8_t *data) {
    for (size_t i = 0; i < TAR_BLOCK; i++)
        if (data[i]) return 0;
    return 1;
}

static uint64_t next_offset(uint64_t offset, const struct tar_header *header) {
    uint64_t size = octal(header->size, sizeof(header->size));
    return offset + TAR_BLOCK + ((size + TAR_BLOCK - 1) & ~(TAR_BLOCK - 1));
}

static int header_at(uint64_t offset, const struct tar_header **header) {
    if (offset > tar_size || tar_size - offset < TAR_BLOCK) return 0;
    const struct tar_header *candidate = (const struct tar_header *)(tar_data + offset);
    if (zero_block((const uint8_t *)candidate)) return 0;
    uint64_t next = next_offset(offset, candidate);
    if (next < offset || next > tar_size) return 0;
    *header = candidate;
    return 1;
}

static size_t tar_path(const struct tar_header *header, char *output) {
    size_t length = 0;
    for (size_t i = 0; i < sizeof(header->prefix) && header->prefix[i]; i++)
        output[length++] = header->prefix[i];
    if (length) output[length++] = '/';
    for (size_t i = 0; i < sizeof(header->name) && header->name[i]; i++)
        output[length++] = header->name[i];
    output[length] = 0;
    return length;
}

static struct vnode *child_named(struct vnode *directory, const char *name, size_t length) {
    for (struct vnode *child = directory->children; child; child = child->next)
        if (name_eq(child->name, name, length)) return child;
    return 0;
}

static void attach(struct vnode *directory, struct vnode *node) {
    struct vnode **link = &directory->children;
    while (*link) link = &(*link)->next;
    node->next = 0;
    node->parent = directory;
    node->linked = 1;
    *link = node;
}

static void detach(struct vnode *node) {
    struct vnode **link = &node->parent->children;
    while (*link != node) link = &(*link)->next;
    *link = node->next;
    node->next = 0;
    node->parent = 0;
    node->linked = 0;
}

static char *copy_name(const char *name, size_t length) {
    char *copy = kmalloc(length + 1);
    if (!copy) return 0;
    memcpy(copy, name, length);
    copy[length] = 0;
    return copy;
}

static struct vnode *new_node(struct vnode *directory, const char *name, size_t length,
                              uint8_t type, uint32_t mode) {
    struct vnode *node = kzalloc(sizeof(*node));
    if (!node) return 0;
    node->name = copy_name(name, length);
    if (!node->name) {
        kfree(node);
        return 0;
    }
    node->ino = next_inode++;
    node->type = type;
    node->mode = mode;
    attach(directory, node);
    return node;
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

static void release_pages(struct vnode *node) {
    if (node->pages.root) tree_free(node->pages.root, node->pages.depth);
    node->pages.root = 0;
    node->pages.depth = 0;
}

static uint8_t *file_page(struct vnode *node, uint64_t index, int create) {
    uint64_t *slot = tree_slot(&node->pages, index, create);
    if (!slot) return 0;
    if (!*slot && (!create || !(*slot = pmm_alloc_page()))) return 0;
    return phys_to_virt(*slot);
}

static void release_node(struct vnode *node) {
    release_pages(node);
    kfree(node->name);
    kfree(node);
}

static void remove_node(struct vnode *node) {
    detach(node);
    if (!node->refs) release_node(node);
}

static enum vfs_status walk(const char *path, size_t length, struct vnode **result) {
    if (!path || !length || path[0] != '/') return VFS_INVALID;
    struct vnode *node = root;
    size_t i = 0;
    while (i < length) {
        while (i < length && path[i] == '/') i++;
        size_t start = i;
        while (i < length && path[i] != '/') i++;
        if (i == start) break;
        if (node->type != VFS_DIR) return VFS_NOT_DIRECTORY;
        if (dot_name(path + start, i - start)) {
            if (i - start == 2 && node->parent) node = node->parent;
            continue;
        }
        node = child_named(node, path + start, i - start);
        if (!node) return VFS_NOT_FOUND;
    }
    *result = node;
    return VFS_OK;
}

static enum vfs_status lookup(const char *path, struct vnode **result) {
    return walk(path, path ? str_len(path) : 0, result);
}

static enum vfs_status lookup_parent(const char *path, struct vnode **parent,
                                     const char **name, size_t *length) {
    if (!path || path[0] != '/') return VFS_INVALID;
    size_t end = str_len(path);
    while (end && path[end - 1] == '/') end--;
    size_t start = end;
    while (start && path[start - 1] != '/') start--;
    if (start == end || dot_name(path + start, end - start)) return VFS_INVALID;
    if (end - start >= VFS_NAME_MAX) return VFS_INVALID;
    enum vfs_status status = walk(path, start, parent);
    if (status != VFS_OK) return status;
    if ((*parent)->type != VFS_DIR) return VFS_NOT_DIRECTORY;
    *name = path + start;
    *length = end - start;
    return VFS_OK;
}

static int detach_archive(struct vnode *node) {
    if (!node->archive) return 0;
    for (uint64_t offset = 0; offset < node->size; offset += PAGE_SIZE) {
        uint8_t *page = file_page(node, offset / PAGE_SIZE, 1);
        if (!page) return -1;
        size_t amount = node->size - offset > PAGE_SIZE ? PAGE_SIZE :
                        (size_t)(node->size - offset);
        memcpy(page, node->archive + offset, amount);
    }
    node->archive = 0;
    return 0;
}

static struct vnode *tar_node(const char *path, uint8_t type, uint32_t mode) {
    struct vnode *node = root;
    const char *cursor = path;
    for (;;) {
        while (*cursor == '/') cursor++;
        const char *start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        size_t length = (size_t)(cursor - start);
        if (!length) return node;
        while (*cursor == '/') cursor++;
        int last = !*cursor;
        if (length == 1 && start[0] == '.') {
            if (last) return node;
            continue;
        }
        if (dot_name(start, length) || length >= VFS_NAME_MAX || node->type != VFS_DIR) return 0;
        struct vnode *child = child_named(node, start, length);
        if (!child) child = new_node(node, start, length, last ? type : VFS_DIR,
                                     last ? mode : (S_IFDIR | 0755));
        if (!child || (last && child->type != type)) return 0;
        if (last) {
            child->mode = mode;
            return child;
        }
        node = child;
    }
}

int vfs_init(const void *archive, uint64_t size) {
    if (!archive || size < TAR_BLOCK * 2) return -1;
    tar_data = archive;
    tar_size = size;
    next_inode = 2;
    root = kzalloc(sizeof(*root));
    if (!root || !(root->name = copy_name("", 0))) return -1;
    root->ino = 1;
    root->type = VFS_DIR;
    root->mode = S_IFDIR | 0755;
    root->linked = 1;

    const struct tar_header *header;
    for (uint64_t offset = 0; header_at(offset, &header); offset = next_offset(offset, header)) {
        if (header->magic[0] != 'u' || header->magic[1] != 's' || header->magic[2] != 't' ||
            header->magic[3] != 'a' || header->magic[4] != 'r') return -1;
        char path[sizeof(header->prefix) + sizeof(header->name) + 2];
        tar_path(header, path);
        uint8_t type = header->type == '5' ? VFS_DIR : VFS_REG;
        uint32_t mode = (type == VFS_DIR ? S_IFDIR : S_IFREG) |
                        (uint32_t)(octal(header->mode, sizeof(header->mode)) & 0777);
        struct vnode *node = tar_node(path, type, mode);
        if (!node || node->type != type) return -1;
        if (type != VFS_REG) continue;
        node->size = octal(header->size, sizeof(header->size));
        node->archive = (const uint8_t *)header + TAR_BLOCK;
    }
    return 0;
}

enum vfs_status vfs_open(const char *path, uint32_t flags, struct file *file) {
    if (!file) return VFS_INVALID;
    struct vnode *node;
    enum vfs_status status = lookup(path, &node);
    if (status == VFS_NOT_FOUND && (flags & VFS_OPEN_CREATE)) {
        struct vnode *parent;
        const char *name;
        size_t length;
        status = lookup_parent(path, &parent, &name, &length);
        if (status != VFS_OK) return status;
        node = new_node(parent, name, length, VFS_REG, S_IFREG | 0666);
        if (!node) return VFS_NO_SPACE;
    } else if (status != VFS_OK) {
        return status;
    } else if ((flags & VFS_OPEN_CREATE) && (flags & VFS_OPEN_EXCLUSIVE)) {
        return VFS_EXISTS;
    }
    if ((flags & VFS_OPEN_DIRECTORY) && node->type != VFS_DIR)
        return VFS_NOT_DIRECTORY;
    if ((flags & VFS_OPEN_WRITE) && node->type == VFS_DIR) return VFS_IS_DIRECTORY;
    if ((flags & VFS_OPEN_TRUNCATE) && (flags & VFS_OPEN_WRITE)) {
        release_pages(node);
        node->archive = 0;
        node->size = 0;
    }
    memset(file, 0, sizeof(*file));
    file->node = node;
    file->flags = flags;
    file->offset = (flags & VFS_OPEN_APPEND) ? node->size : 0;
    node->refs++;
    return VFS_OK;
}

void vfs_close(struct file *file) {
    if (!file || !file->node) return;
    struct vnode *node = file->node;
    if (node->refs) node->refs--;
    if (!node->linked && !node->refs) release_node(node);
    file->node = 0;
}

void vfs_file_info(const struct file *file, struct vfs_info *info) {
    info->size = file->node->size;
    info->ino = file->node->ino;
    info->mode = file->node->mode;
    info->type = file->node->type;
}

long vfs_read(struct file *file, void *buffer, size_t length) {
    if (!file || !file->node || file->node->type != VFS_REG ||
        !(file->flags & VFS_OPEN_READ))
        return -1;
    struct vnode *node = file->node;
    if (file->offset >= node->size) return 0;
    uint64_t available = node->size - file->offset;
    if ((uint64_t)length > available) length = (size_t)available;
    size_t done = 0;
    while (done < length) {
        uint64_t position = file->offset + done;
        size_t in_page = (size_t)(position & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - in_page;
        if (chunk > length - done) chunk = length - done;
        const uint8_t *source = node->archive ? node->archive + position :
                                file_page(node, position / PAGE_SIZE, 0);
        if (!source) memset((uint8_t *)buffer + done, 0, chunk);
        else memcpy((uint8_t *)buffer + done, source + (node->archive ? 0 : in_page), chunk);
        done += chunk;
    }
    file->offset += done;
    return (long)done;
}

long vfs_write(struct file *file, const void *buffer, size_t length) {
    if (!file || !file->node || file->node->type != VFS_REG ||
        !(file->flags & VFS_OPEN_WRITE))
        return -1;
    struct vnode *node = file->node;
    if (file->flags & VFS_OPEN_APPEND) file->offset = node->size;
    if ((uint64_t)length > (uint64_t)INT64_MAX - file->offset) return -1;
    if (detach_archive(node) != 0) return -1;
    size_t done = 0;
    while (done < length) {
        uint64_t position = file->offset + done;
        size_t in_page = (size_t)(position & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - in_page;
        if (chunk > length - done) chunk = length - done;
        uint8_t *page = file_page(node, position / PAGE_SIZE, 1);
        if (!page) break;
        memcpy(page + in_page, (const uint8_t *)buffer + done, chunk);
        done += chunk;
    }
    if (!done && length) return -1;
    file->offset += done;
    if (file->offset > node->size) node->size = file->offset;
    return (long)done;
}

long vfs_seek(struct file *file, int64_t offset, int origin) {
    if (!file || !file->node || file->node->type != VFS_REG) return -1;
    int64_t base = origin == 0 ? 0 : origin == 1 ? (int64_t)file->offset :
                   origin == 2 ? (int64_t)file->node->size : -1;
    if (base < 0 || offset < -base || (offset > 0 && base > INT64_MAX - offset)) return -1;
    file->offset = (uint64_t)(base + offset);
    return (long)file->offset;
}

int vfs_readdir(struct file *file, struct vfs_dirent *entry) {
    if (!file || !file->node || !entry || file->node->type != VFS_DIR) return -1;
    struct vnode *child = file->node->children;
    for (uint64_t i = 0; child && i < file->offset; i++) child = child->next;
    if (!child) return 0;
    file->offset++;
    entry->ino = child->ino;
    entry->type = child->type;
    memcpy(entry->name, child->name, str_len(child->name) + 1);
    return 1;
}

enum vfs_status vfs_mkdir(const char *path, uint32_t mode) {
    struct vnode *parent;
    const char *name;
    size_t length;
    enum vfs_status status = lookup_parent(path, &parent, &name, &length);
    if (status != VFS_OK) return status;
    if (child_named(parent, name, length)) return VFS_EXISTS;
    if (!new_node(parent, name, length, VFS_DIR, S_IFDIR | (mode & 0777))) return VFS_NO_SPACE;
    return VFS_OK;
}

enum vfs_status vfs_unlink(const char *path, int remove_directory) {
    struct vnode *parent;
    const char *name;
    size_t length;
    enum vfs_status status = lookup_parent(path, &parent, &name, &length);
    if (status != VFS_OK) return status;
    struct vnode *node = child_named(parent, name, length);
    if (!node) return VFS_NOT_FOUND;
    if (remove_directory && node->type != VFS_DIR) return VFS_NOT_DIRECTORY;
    if (!remove_directory && node->type == VFS_DIR) return VFS_IS_DIRECTORY;
    if (node->children) return VFS_NOT_EMPTY;
    remove_node(node);
    return VFS_OK;
}

enum vfs_status vfs_rename(const char *old_path, const char *new_path) {
    struct vnode *old_parent, *new_parent;
    const char *old_name, *new_name;
    size_t old_length, new_length;
    enum vfs_status status = lookup_parent(old_path, &old_parent, &old_name, &old_length);
    if (status != VFS_OK) return status;
    struct vnode *node = child_named(old_parent, old_name, old_length);
    if (!node) return VFS_NOT_FOUND;
    status = lookup_parent(new_path, &new_parent, &new_name, &new_length);
    if (status != VFS_OK) return status;
    for (struct vnode *ancestor = new_parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == node) return VFS_INVALID;

    struct vnode *target = child_named(new_parent, new_name, new_length);
    if (target == node) return VFS_OK;
    if (target) {
        if (node->type == VFS_DIR && target->type != VFS_DIR)
            return VFS_NOT_DIRECTORY;
        if (node->type != VFS_DIR && target->type == VFS_DIR)
            return VFS_IS_DIRECTORY;
        if (target->children) return VFS_NOT_EMPTY;
    }
    char *name = copy_name(new_name, new_length);
    if (!name) return VFS_NO_SPACE;
    if (target) remove_node(target);
    detach(node);
    kfree(node->name);
    node->name = name;
    attach(new_parent, node);
    return VFS_OK;
}
