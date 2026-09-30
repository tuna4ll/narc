#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define TAR_BLOCK 512
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define VFS_NODE_MAX 128
#define VFS_PAGE_MAX (VFS_FILE_MAX / PAGE_SIZE)

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

struct vnode {
    int used;
    int linked;
    uint32_t refs;
    uint64_t size;
    uint64_t ino;
    uint32_t mode;
    uint8_t type;
    char path[VFS_PATH_MAX];
    const uint8_t *archive;
    uint64_t pages[VFS_PAGE_MAX];
};

static const uint8_t *tar_data;
static uint64_t tar_size;
static uint64_t next_inode;
static struct vnode nodes[VFS_NODE_MAX];

static size_t str_len(const char *string) {
    size_t length = 0;
    while (string[length]) length++;
    return length;
}

static int str_eq(const char *left, const char *right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static int str_prefix(const char *string, const char *prefix) {
    while (*prefix && *string == *prefix) {
        string++;
        prefix++;
    }
    return !*prefix;
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

static int make_path(const struct tar_header *header, char *output) {
    char raw[VFS_PATH_MAX];
    size_t length = 0;
    if (header->prefix[0]) {
        while (length < sizeof(header->prefix) && header->prefix[length] &&
               length + 1 < sizeof(raw)) {
            raw[length] = header->prefix[length];
            length++;
        }
        if (length + 1 >= sizeof(raw)) return -1;
        raw[length++] = '/';
    }
    for (size_t i = 0; i < sizeof(header->name) && header->name[i]; i++) {
        if (length + 1 >= sizeof(raw)) return -1;
        raw[length++] = header->name[i];
    }
    raw[length] = 0;

    const char *source = raw;
    while (source[0] == '.' && source[1] == '/') source += 2;
    while (*source == '/') source++;
    output[0] = '/';
    length = 1;
    while (*source && length + 1 < VFS_PATH_MAX) output[length++] = *source++;
    if (*source) return -1;
    while (length > 1 && output[length - 1] == '/') length--;
    output[length] = 0;
    return 0;
}

static int normalize(const char *path, char *output) {
    if (!path || *path != '/') return -1;
    size_t length = 1;
    output[0] = '/';
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        const char *component = path;
        while (*path && *path != '/') path++;
        size_t component_length = (size_t)(path - component);
        if (component_length == 1 && component[0] == '.') continue;
        if (component_length == 2 && component[0] == '.' && component[1] == '.') {
            while (length > 1 && output[length - 1] != '/') length--;
            if (length > 1) length--;
            continue;
        }
        if (component_length >= VFS_NAME_MAX ||
            component_length + length + (length > 1) >= VFS_PATH_MAX)
            return -1;
        if (length > 1) output[length++] = '/';
        memcpy(output + length, component, component_length);
        length += component_length;
    }
    output[length] = 0;
    return 0;
}

static struct vnode *find_node(const char *path) {
    for (size_t i = 0; i < VFS_NODE_MAX; i++)
        if (nodes[i].used && nodes[i].linked && str_eq(nodes[i].path, path))
            return &nodes[i];
    return 0;
}

static struct vnode *new_node(void) {
    for (size_t i = 0; i < VFS_NODE_MAX; i++) {
        if (nodes[i].used) continue;
        memset(&nodes[i], 0, sizeof(nodes[i]));
        nodes[i].used = 1;
        nodes[i].linked = 1;
        nodes[i].ino = next_inode++;
        return &nodes[i];
    }
    return 0;
}

static void release_pages(struct vnode *node) {
    for (size_t i = 0; i < VFS_PAGE_MAX; i++) {
        if (node->pages[i]) pmm_free_page(node->pages[i]);
        node->pages[i] = 0;
    }
}

static void release_node(struct vnode *node) {
    release_pages(node);
    memset(node, 0, sizeof(*node));
}

static int parent_path(const char *path, char *parent) {
    size_t length = str_len(path);
    if (length <= 1) return -1;
    while (length > 1 && path[length - 1] != '/') length--;
    if (length == 1) {
        parent[0] = '/';
        parent[1] = 0;
        return 0;
    }
    memcpy(parent, path, length - 1);
    parent[length - 1] = 0;
    return 0;
}

static enum vfs_status validate_parent(const char *path) {
    char parent[VFS_PATH_MAX];
    if (parent_path(path, parent) != 0) return VFS_INVALID;
    struct vnode *node = find_node(parent);
    if (!node) return VFS_NOT_FOUND;
    return node->type == VFS_DIR ? VFS_OK : VFS_NOT_DIRECTORY;
}

static int ensure_pages(struct vnode *node, uint64_t end) {
    if (end > VFS_FILE_MAX) return -1;
    size_t count = (size_t)((end + PAGE_SIZE - 1) / PAGE_SIZE);
    for (size_t i = 0; i < count; i++) {
        if (node->pages[i]) continue;
        node->pages[i] = pmm_alloc_page();
        if (!node->pages[i]) return -1;
    }
    if (node->archive) {
        for (uint64_t offset = 0; offset < node->size; offset += PAGE_SIZE) {
            size_t amount = node->size - offset > PAGE_SIZE ? PAGE_SIZE :
                            (size_t)(node->size - offset);
            memcpy(phys_to_virt(node->pages[offset / PAGE_SIZE]),
                   node->archive + offset, amount);
        }
        node->archive = 0;
    }
    return 0;
}

static void copy_path(char *destination, const char *source) {
    size_t length = str_len(source);
    memcpy(destination, source, length + 1);
}

int vfs_init(const void *archive, uint64_t size) {
    if (!archive || size < TAR_BLOCK * 2) return -1;
    tar_data = archive;
    tar_size = size;
    next_inode = 2;
    memset(nodes, 0, sizeof(nodes));

    struct vnode *root = new_node();
    if (!root) return -1;
    root->ino = 1;
    root->type = VFS_DIR;
    root->mode = S_IFDIR | 0755;
    copy_path(root->path, "/");

    const struct tar_header *header;
    for (uint64_t offset = 0; header_at(offset, &header); offset = next_offset(offset, header)) {
        if (header->magic[0] != 'u' || header->magic[1] != 's' || header->magic[2] != 't' ||
            header->magic[3] != 'a' || header->magic[4] != 'r') return -1;
        char path[VFS_PATH_MAX];
        if (make_path(header, path) != 0) return -1;
        if (str_eq(path, "/")) continue;
        struct vnode *node = new_node();
        if (!node) return -1;
        node->type = header->type == '5' ? VFS_DIR : VFS_REG;
        node->mode = (node->type == VFS_DIR ? S_IFDIR : S_IFREG) |
                     (uint32_t)(octal(header->mode, sizeof(header->mode)) & 0777);
        node->size = node->type == VFS_REG ? octal(header->size, sizeof(header->size)) : 0;
        if (node->size > VFS_FILE_MAX) return -1;
        node->archive = node->type == VFS_REG ? (const uint8_t *)header + TAR_BLOCK : 0;
        copy_path(node->path, path);
    }
    return 0;
}

enum vfs_status vfs_open(const char *path, uint32_t flags, struct file *file) {
    char normalized[VFS_PATH_MAX];
    if (!file || normalize(path, normalized) != 0) return VFS_INVALID;
    struct vnode *node = find_node(normalized);
    if (!node && !(flags & VFS_OPEN_CREATE)) return VFS_NOT_FOUND;
    if (!node) {
        enum vfs_status status = validate_parent(normalized);
        if (status != VFS_OK) return status;
        node = new_node();
        if (!node) return VFS_NO_SPACE;
        node->type = VFS_REG;
        node->mode = S_IFREG | 0666;
        copy_path(node->path, normalized);
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
        size_t chunk = PAGE_SIZE - (size_t)(position & (PAGE_SIZE - 1));
        if (chunk > length - done) chunk = length - done;
        const uint8_t *source = node->archive ? node->archive + position :
                                phys_to_virt(node->pages[position / PAGE_SIZE]);
        memcpy((uint8_t *)buffer + done, source, chunk);
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
    if ((uint64_t)length > VFS_FILE_MAX - file->offset) return -1;
    uint64_t end = file->offset + length;
    if (ensure_pages(node, end > node->size ? end : node->size) != 0) return -1;
    size_t done = 0;
    while (done < length) {
        uint64_t position = file->offset + done;
        size_t chunk = PAGE_SIZE - (size_t)(position & (PAGE_SIZE - 1));
        if (chunk > length - done) chunk = length - done;
        uint8_t *destination = phys_to_virt(node->pages[position / PAGE_SIZE]);
        memcpy(destination + (position & (PAGE_SIZE - 1)),
               (const uint8_t *)buffer + done, chunk);
        done += chunk;
    }
    file->offset = end;
    if (end > node->size) node->size = end;
    return (long)done;
}

long vfs_seek(struct file *file, int64_t offset, int origin) {
    if (!file || !file->node || file->node->type != VFS_REG) return -1;
    int64_t base = origin == 0 ? 0 : origin == 1 ? (int64_t)file->offset :
                   origin == 2 ? (int64_t)file->node->size : -1;
    if (base < 0 || offset < -base) return -1;
    uint64_t next = (uint64_t)(base + offset);
    if (next > VFS_FILE_MAX) return -1;
    file->offset = next;
    return (long)next;
}

static int direct_child(const char *directory, const char *path, const char **name) {
    if (str_eq(directory, "/")) {
        if (path[0] != '/' || !path[1]) return 0;
        *name = path + 1;
    } else {
        size_t length = str_len(directory);
        if (!str_prefix(path, directory) || path[length] != '/') return 0;
        *name = path + length + 1;
    }
    for (const char *cursor = *name; *cursor; cursor++)
        if (*cursor == '/') return 0;
    return **name != 0;
}

int vfs_readdir(struct file *file, struct vfs_dirent *entry) {
    if (!file || !file->node || !entry || file->node->type != VFS_DIR) return -1;
    while (file->offset < VFS_NODE_MAX) {
        struct vnode *node = &nodes[file->offset++];
        const char *name;
        if (!node->used || !node->linked || !direct_child(file->node->path, node->path, &name))
            continue;
        entry->ino = node->ino;
        entry->type = node->type;
        copy_path(entry->name, name);
        return 1;
    }
    return 0;
}

enum vfs_status vfs_mkdir(const char *path, uint32_t mode) {
    char normalized[VFS_PATH_MAX];
    if (normalize(path, normalized) != 0 || str_eq(normalized, "/")) return VFS_INVALID;
    if (find_node(normalized)) return VFS_EXISTS;
    enum vfs_status status = validate_parent(normalized);
    if (status != VFS_OK) return status;
    struct vnode *node = new_node();
    if (!node) return VFS_NO_SPACE;
    node->type = VFS_DIR;
    node->mode = S_IFDIR | (mode & 0777);
    copy_path(node->path, normalized);
    return VFS_OK;
}

static int directory_empty(const struct vnode *directory) {
    for (size_t i = 0; i < VFS_NODE_MAX; i++) {
        const char *name;
        if (nodes[i].used && nodes[i].linked &&
            direct_child(directory->path, nodes[i].path, &name))
            return 0;
    }
    return 1;
}

enum vfs_status vfs_unlink(const char *path, int remove_directory) {
    char normalized[VFS_PATH_MAX];
    if (normalize(path, normalized) != 0 || str_eq(normalized, "/")) return VFS_INVALID;
    struct vnode *node = find_node(normalized);
    if (!node) return VFS_NOT_FOUND;
    if (remove_directory && node->type != VFS_DIR) return VFS_NOT_DIRECTORY;
    if (!remove_directory && node->type == VFS_DIR) return VFS_IS_DIRECTORY;
    if (node->type == VFS_DIR && !directory_empty(node)) return VFS_NOT_EMPTY;
    node->linked = 0;
    if (!node->refs) release_node(node);
    return VFS_OK;
}

enum vfs_status vfs_rename(const char *old_path, const char *new_path) {
    char old_normalized[VFS_PATH_MAX], new_normalized[VFS_PATH_MAX];
    if (normalize(old_path, old_normalized) != 0 ||
        normalize(new_path, new_normalized) != 0 || str_eq(old_normalized, "/") ||
        str_eq(new_normalized, "/"))
        return VFS_INVALID;
    struct vnode *node = find_node(old_normalized);
    if (!node) return VFS_NOT_FOUND;
    if (str_eq(old_normalized, new_normalized)) return VFS_OK;
    size_t old_length = str_len(old_normalized);
    if (node->type == VFS_DIR && str_prefix(new_normalized, old_normalized) &&
        new_normalized[old_length] == '/')
        return VFS_INVALID;
    enum vfs_status status = validate_parent(new_normalized);
    if (status != VFS_OK) return status;

    struct vnode *target = find_node(new_normalized);
    if (target) {
        if (node->type == VFS_DIR && target->type != VFS_DIR)
            return VFS_NOT_DIRECTORY;
        if (node->type != VFS_DIR && target->type == VFS_DIR)
            return VFS_IS_DIRECTORY;
        if (target->type == VFS_DIR && !directory_empty(target))
            return VFS_NOT_EMPTY;
    }

    size_t new_length = str_len(new_normalized);
    for (size_t i = 0; i < VFS_NODE_MAX; i++) {
        if (!nodes[i].used || !nodes[i].linked) continue;
        if (!str_eq(nodes[i].path, old_normalized) &&
            !(str_prefix(nodes[i].path, old_normalized) && nodes[i].path[old_length] == '/'))
            continue;
        size_t suffix = str_len(nodes[i].path + old_length);
        if (new_length + suffix >= VFS_PATH_MAX) return VFS_INVALID;
    }
    if (target) {
        target->linked = 0;
        if (!target->refs) release_node(target);
    }
    for (size_t i = 0; i < VFS_NODE_MAX; i++) {
        if (!nodes[i].used || !nodes[i].linked) continue;
        if (!str_eq(nodes[i].path, old_normalized) &&
            !(str_prefix(nodes[i].path, old_normalized) && nodes[i].path[old_length] == '/'))
            continue;
        char renamed[VFS_PATH_MAX];
        memcpy(renamed, new_normalized, new_length);
        copy_path(renamed + new_length, nodes[i].path + old_length);
        copy_path(nodes[i].path, renamed);
    }
    return VFS_OK;
}
