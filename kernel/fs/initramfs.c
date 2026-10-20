#include <kernel/string.h>
#include <kernel/vfs.h>
#include <narc/abi.h>

#define TAR_BLOCK 512

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

static int child(struct vnode *dir, const char *name, size_t length, uint32_t mode,
                 struct vnode **result) {
    int status = dir->ops->lookup(dir, name, length, result);
    if (status != -NARC_NOT_FOUND) return status;
    return dir->ops->create(dir, name, length, mode, 0, result);
}

static int place(struct vnode *root, const char *path, uint32_t mode, struct vnode **result) {
    struct vnode *node = vnode_get(root);
    const char *cursor = path;
    for (;;) {
        while (*cursor == '/') cursor++;
        const char *start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        size_t length = (size_t)(cursor - start);
        while (*cursor == '/') cursor++;
        int last = !*cursor;
        if (!length || (length == 1 && start[0] == '.')) {
            if (!last) continue;
            *result = node;
            return 0;
        }
        if ((length == 2 && start[0] == '.' && start[1] == '.') || length > VFS_NAME_MAX ||
            node->type != VFS_DIR)
            break;
        struct vnode *next;
        int status = child(node, start, length, last ? mode : (S_IFDIR | 0755), &next);
        vnode_put(node);
        if (status) return status;
        node = next;
        if (last) {
            if (node->type != vfs_mode_type(mode)) break;
            node->mode = mode;
            *result = node;
            return 0;
        }
    }
    vnode_put(node);
    return -NARC_INVALID_ARGUMENT;
}

int initramfs_unpack(struct vnode *root, const void *archive, uint64_t size) {
    const uint8_t *data = archive;
    if (!archive || size < TAR_BLOCK * 2) return -NARC_INVALID_ARGUMENT;
    for (uint64_t offset = 0; size - offset >= TAR_BLOCK;) {
        const struct tar_header *header = (const struct tar_header *)(data + offset);
        if (zero_block(data + offset)) break;
        if (memcmp(header->magic, "ustar", 5)) return -NARC_INVALID_ARGUMENT;
        uint64_t length = octal(header->size, sizeof(header->size));
        uint64_t next = offset + TAR_BLOCK + ((length + TAR_BLOCK - 1) & ~(uint64_t)(TAR_BLOCK - 1));
        if (next < offset || next > size) return -NARC_INVALID_ARGUMENT;

        uint32_t permissions = (uint32_t)(octal(header->mode, sizeof(header->mode)) & 07777);
        uint32_t mode;
        char path[sizeof(header->prefix) + sizeof(header->name) + 2];
        tar_path(header, path);
        if (header->type == '1' || header->type == '2') {
            char link[sizeof(header->link) + 1];
            memcpy(link, header->link, sizeof(header->link));
            link[sizeof(header->link)] = 0;
            int status = header->type == '2' ? vfs_symlink(root, link, path)
                                             : vfs_link(root, link, path);
            if (status && status != -NARC_EXISTS) return status;
            offset = next;
            continue;
        }
        if (header->type == '5') mode = S_IFDIR | permissions;
        else if (header->type == '0' || !header->type) mode = S_IFREG | permissions;
        else {
            offset = next;
            continue;
        }
        struct vnode *node;
        int status = place(root, path, mode, &node);
        if (status) return status;
        if (node->type == VFS_REG) status = tmpfs_attach_archive(node, header + 1, length);
        vnode_put(node);
        if (status) return status;
        offset = next;
    }
    return 0;
}
