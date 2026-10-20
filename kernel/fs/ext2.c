#include <kernel/block.h>
#include <kernel/heap.h>
#include <kernel/mutex.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <kernel/vfs.h>
#include <narc/abi.h>

#define EXT2_MAGIC 0xef53
#define EXT2_ROOT 2
#define EXT2_DIRECT 12
#define EXT2_INDEX_FLAG 0x1000
#define INCOMPAT_FILETYPE 0x0002
#define RO_COMPAT_SPARSE 0x0001
#define RO_COMPAT_LARGE_FILE 0x0002
#define FAST_SYMLINK_MAX 60
#define EXT2_NAME_MAX 255

struct ext2_super {
    uint32_t inodes_count, blocks_count, reserved_blocks, free_blocks, free_inodes;
    uint32_t first_data_block, log_block_size, log_fragment_size;
    uint32_t blocks_per_group, fragments_per_group, inodes_per_group;
    uint32_t mount_time, write_time;
    uint16_t mount_count, max_mount_count, magic, state, errors, minor_revision;
    uint32_t last_check, check_interval, creator_os, revision;
    uint16_t reserved_uid, reserved_gid;
    uint32_t first_inode;
    uint16_t inode_size, block_group;
    uint32_t compat, incompat, ro_compat;
    uint8_t rest[1024 - 104];
};

struct ext2_group {
    uint32_t block_bitmap, inode_bitmap, inode_table;
    uint16_t free_blocks, free_inodes, used_directories, padding;
    uint8_t reserved[12];
};

struct ext2_inode {
    uint16_t mode, uid;
    uint32_t size, atime, ctime, mtime, dtime;
    uint16_t gid, links;
    uint32_t sectors, flags, os1;
    uint32_t block[15];
    uint32_t generation, file_acl, size_high, fragment;
    uint8_t os2[12];
};

_Static_assert(sizeof(struct ext2_super) == 1024, "ext2 superblock layout");
_Static_assert(sizeof(struct ext2_group) == 32, "ext2 group layout");
_Static_assert(sizeof(struct ext2_inode) == 128, "ext2 inode layout");

struct ext2_dirent {
    uint32_t inode;
    uint16_t record_length;
    uint8_t name_length, type;
    char name[];
} __attribute__((packed));

struct ext2_fs {
    struct block_device *device;
    struct mount *mount;
    struct ext2_super super;
    struct ext2_group *groups;
    uint32_t group_count, block_size, inode_size, pointers;
    int filetype, large_file;
    struct mutex lock;
    struct ext2_node *nodes;
    int unmounting;
};

struct ext2_node {
    struct vnode vnode;
    struct ext2_node *next;
    struct ext2_fs *fs;
    uint32_t ino;
    struct ext2_inode raw;
};

static const struct vnode_ops ext2_ops;

static struct ext2_node *enode(struct vnode *vnode) {
    return (struct ext2_node *)vnode;
}

static uint16_t load16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static void store16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint32_t now(void) {
    return (uint32_t)(time_realtime_ns() / (int64_t)NSEC_PER_SEC);
}

static int read_block(struct ext2_fs *fs, uint32_t block, void *data) {
    struct buffer *buffer = bread(fs->device, block, fs->block_size);
    if (!buffer) return -NARC_IO_ERROR;
    memcpy(data, buffer->data, fs->block_size);
    brelse(buffer);
    return 0;
}

static int write_block(struct ext2_fs *fs, uint32_t block, const void *data) {
    struct buffer *buffer = bread(fs->device, block, fs->block_size);
    if (!buffer) return -NARC_IO_ERROR;
    memcpy(buffer->data, data, fs->block_size);
    int status = bwrite(buffer);
    brelse(buffer);
    return status;
}

static int update_bytes(struct ext2_fs *fs, uint32_t block, uint32_t offset, const void *data,
                        size_t length) {
    struct buffer *buffer = bread(fs->device, block, fs->block_size);
    if (!buffer) return -NARC_IO_ERROR;
    memcpy(buffer->data + offset, data, length);
    int status = bwrite(buffer);
    brelse(buffer);
    return status;
}

static int write_super(struct ext2_fs *fs) {
    fs->super.write_time = now();
    uint32_t block = fs->block_size == 1024 ? 1 : 0;
    uint32_t offset = fs->block_size == 1024 ? 0 : 1024;
    return update_bytes(fs, block, offset, &fs->super, sizeof(fs->super));
}

static int write_group(struct ext2_fs *fs, uint32_t group) {
    uint32_t per_block = fs->block_size / sizeof(struct ext2_group);
    uint32_t block = fs->super.first_data_block + 1 + group / per_block;
    uint32_t offset = (group % per_block) * sizeof(struct ext2_group);
    return update_bytes(fs, block, offset, &fs->groups[group], sizeof(struct ext2_group));
}

static int inode_location(struct ext2_fs *fs, uint32_t ino, uint32_t *block, uint32_t *offset) {
    if (!ino || ino > fs->super.inodes_count) return -NARC_IO_ERROR;
    uint32_t group = (ino - 1) / fs->super.inodes_per_group;
    uint32_t index = (ino - 1) % fs->super.inodes_per_group;
    uint64_t byte = (uint64_t)index * fs->inode_size;
    *block = fs->groups[group].inode_table + (uint32_t)(byte / fs->block_size);
    *offset = (uint32_t)(byte % fs->block_size);
    return 0;
}

static int read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *inode) {
    uint32_t block, offset;
    int status = inode_location(fs, ino, &block, &offset);
    if (status) return status;
    struct buffer *buffer = bread(fs->device, block, fs->block_size);
    if (!buffer) return -NARC_IO_ERROR;
    memcpy(inode, buffer->data + offset, sizeof(*inode));
    brelse(buffer);
    return 0;
}

static int write_inode(struct ext2_node *node) {
    struct ext2_fs *fs = node->fs;
    struct vnode *v = &node->vnode;
    node->raw.mode = (uint16_t)v->mode;
    node->raw.links = (uint16_t)v->nlink;
    node->raw.uid = (uint16_t)v->uid;
    node->raw.gid = (uint16_t)v->gid;
    store16(node->raw.os2 + 4, (uint16_t)(v->uid >> 16));
    store16(node->raw.os2 + 6, (uint16_t)(v->gid >> 16));
    node->raw.size = (uint32_t)v->size;
    if (v->type == VFS_REG) node->raw.size_high = (uint32_t)(v->size >> 32);
    node->raw.atime = (uint32_t)v->atime;
    node->raw.mtime = (uint32_t)v->mtime;
    node->raw.ctime = (uint32_t)v->ctime;
    uint32_t block, offset;
    int status = inode_location(fs, node->ino, &block, &offset);
    if (status) return status;
    return update_bytes(fs, block, offset, &node->raw, sizeof(node->raw));
}

static int bitmap_allocate(struct ext2_fs *fs, uint32_t bitmap, uint32_t limit, uint32_t first) {
    struct buffer *buffer = bread(fs->device, bitmap, fs->block_size);
    if (!buffer) return -1;
    int found = -1;
    for (uint32_t bit = first; bit < limit; bit++) {
        if (buffer->data[bit / 8] & (1u << (bit % 8))) continue;
        buffer->data[bit / 8] |= (uint8_t)(1u << (bit % 8));
        found = bwrite(buffer) == 0 ? (int)bit : -1;
        break;
    }
    brelse(buffer);
    return found;
}

static int bitmap_release(struct ext2_fs *fs, uint32_t bitmap, uint32_t bit) {
    struct buffer *buffer = bread(fs->device, bitmap, fs->block_size);
    if (!buffer) return -NARC_IO_ERROR;
    buffer->data[bit / 8] &= (uint8_t)~(1u << (bit % 8));
    int status = bwrite(buffer);
    brelse(buffer);
    return status;
}

static uint32_t allocate_block(struct ext2_fs *fs) {
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (!fs->groups[group].free_blocks) continue;
        uint32_t base = fs->super.first_data_block + group * fs->super.blocks_per_group;
        uint32_t limit = fs->super.blocks_per_group;
        if (base + limit > fs->super.blocks_count) limit = fs->super.blocks_count - base;
        int bit = bitmap_allocate(fs, fs->groups[group].block_bitmap, limit, 0);
        if (bit < 0) continue;
        fs->groups[group].free_blocks--;
        fs->super.free_blocks--;
        write_group(fs, group);
        write_super(fs);
        uint32_t block = base + (uint32_t)bit;
        struct buffer *buffer = bread(fs->device, block, fs->block_size);
        if (buffer) {
            memset(buffer->data, 0, fs->block_size);
            bwrite(buffer);
            brelse(buffer);
        }
        return block;
    }
    return 0;
}

static void free_block(struct ext2_fs *fs, uint32_t block) {
    if (block < fs->super.first_data_block || block >= fs->super.blocks_count) return;
    uint32_t group = (block - fs->super.first_data_block) / fs->super.blocks_per_group;
    uint32_t bit = (block - fs->super.first_data_block) % fs->super.blocks_per_group;
    if (bitmap_release(fs, fs->groups[group].block_bitmap, bit) != 0) return;
    fs->groups[group].free_blocks++;
    fs->super.free_blocks++;
    write_group(fs, group);
    write_super(fs);
}

static uint32_t allocate_inode(struct ext2_fs *fs, int directory) {
    uint32_t first = fs->super.revision ? fs->super.first_inode : 11;
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (!fs->groups[group].free_inodes) continue;
        uint32_t skip = group == 0 ? first - 1 : 0;
        int bit = bitmap_allocate(fs, fs->groups[group].inode_bitmap, fs->super.inodes_per_group,
                                  skip);
        if (bit < 0) continue;
        fs->groups[group].free_inodes--;
        if (directory) fs->groups[group].used_directories++;
        fs->super.free_inodes--;
        write_group(fs, group);
        write_super(fs);
        return group * fs->super.inodes_per_group + (uint32_t)bit + 1;
    }
    return 0;
}

static void free_inode(struct ext2_fs *fs, uint32_t ino, int directory) {
    uint32_t group = (ino - 1) / fs->super.inodes_per_group;
    uint32_t bit = (ino - 1) % fs->super.inodes_per_group;
    if (bitmap_release(fs, fs->groups[group].inode_bitmap, bit) != 0) return;
    fs->groups[group].free_inodes++;
    if (directory && fs->groups[group].used_directories) fs->groups[group].used_directories--;
    fs->super.free_inodes++;
    write_group(fs, group);
    write_super(fs);
}

static uint32_t read_pointer(struct ext2_fs *fs, uint32_t block, uint32_t index) {
    struct buffer *buffer = bread(fs->device, block, fs->block_size);
    if (!buffer) return 0;
    uint32_t value = ((uint32_t *)buffer->data)[index];
    brelse(buffer);
    return value;
}

static int write_pointer(struct ext2_fs *fs, uint32_t block, uint32_t index, uint32_t value) {
    return update_bytes(fs, block, index * 4, &value, 4);
}

static uint32_t child(struct ext2_node *node, uint32_t parent, uint32_t index, int create,
                      int *error) {
    struct ext2_fs *fs = node->fs;
    uint32_t value = read_pointer(fs, parent, index);
    if (value || !create) return value;
    value = allocate_block(fs);
    if (!value || write_pointer(fs, parent, index, value) != 0) {
        *error = 1;
        return 0;
    }
    node->raw.sectors += fs->block_size / 512;
    return value;
}

static uint32_t slot(struct ext2_node *node, uint32_t *entry, int create, int *error) {
    if (*entry || !create) return *entry;
    uint32_t block = allocate_block(node->fs);
    if (!block) {
        *error = 1;
        return 0;
    }
    *entry = block;
    node->raw.sectors += node->fs->block_size / 512;
    return block;
}

static uint32_t bmap(struct ext2_node *node, uint64_t logical, int create) {
    struct ext2_fs *fs = node->fs;
    uint32_t per = fs->pointers;
    int error = 0;
    if (logical < EXT2_DIRECT) return slot(node, &node->raw.block[logical], create, &error);
    logical -= EXT2_DIRECT;
    if (logical < per) {
        uint32_t table = slot(node, &node->raw.block[12], create, &error);
        return table ? child(node, table, (uint32_t)logical, create, &error) : 0;
    }
    logical -= per;
    if (logical < (uint64_t)per * per) {
        uint32_t table = slot(node, &node->raw.block[13], create, &error);
        if (table) table = child(node, table, (uint32_t)(logical / per), create, &error);
        return table ? child(node, table, (uint32_t)(logical % per), create, &error) : 0;
    }
    logical -= (uint64_t)per * per;
    if (logical >= (uint64_t)per * per * per) return 0;
    uint32_t table = slot(node, &node->raw.block[14], create, &error);
    if (table) table = child(node, table, (uint32_t)(logical / ((uint64_t)per * per)), create, &error);
    if (table) table = child(node, table, (uint32_t)(logical / per % per), create, &error);
    return table ? child(node, table, (uint32_t)(logical % per), create, &error) : 0;
}

static int release_tree(struct ext2_node *node, uint32_t block, unsigned depth, uint64_t first,
                        uint64_t keep, uint64_t span) {
    struct ext2_fs *fs = node->fs;
    if (!block) return 1;
    if (!depth) {
        if (first < keep) return 0;
        free_block(fs, block);
        node->raw.sectors -= fs->block_size / 512;
        return 1;
    }
    uint64_t child_span = span / fs->pointers;
    int empty = 1;
    for (uint32_t i = 0; i < fs->pointers; i++) {
        uint64_t start = first + i * child_span;
        uint32_t entry = read_pointer(fs, block, i);
        if (!entry) continue;
        if (start + child_span <= keep) {
            empty = 0;
            continue;
        }
        if (release_tree(node, entry, depth - 1, start, keep, child_span))
            write_pointer(fs, block, i, 0);
        else
            empty = 0;
    }
    if (!empty || first < keep) return 0;
    free_block(fs, block);
    node->raw.sectors -= fs->block_size / 512;
    return 1;
}

static void release_blocks(struct ext2_node *node, uint64_t keep) {
    uint64_t per = node->fs->pointers;
    for (uint32_t i = 0; i < EXT2_DIRECT; i++)
        if (i >= keep && node->raw.block[i] &&
            release_tree(node, node->raw.block[i], 0, i, keep, 1))
            node->raw.block[i] = 0;
    uint64_t first = EXT2_DIRECT;
    uint64_t spans[3] = { per, per * per, per * per * per };
    for (unsigned level = 0; level < 3; level++) {
        uint32_t *entry = &node->raw.block[12 + level];
        if (*entry && release_tree(node, *entry, level + 1, first, keep, spans[level])) *entry = 0;
        first += spans[level];
    }
}

static int fast_symlink(const struct ext2_node *node) {
    return node->vnode.type == VFS_LNK && node->vnode.size < FAST_SYMLINK_MAX &&
           !node->raw.sectors;
}

static struct ext2_node *cached(struct ext2_fs *fs, uint32_t ino) {
    for (struct ext2_node *node = fs->nodes; node; node = node->next)
        if (node->ino == ino) return node;
    return 0;
}

static void fill_vnode(struct ext2_node *node) {
    struct vnode *v = &node->vnode;
    struct ext2_inode *raw = &node->raw;
    vnode_init(v, node->fs->mount, &ext2_ops, raw->mode);
    v->ino = node->ino;
    v->nlink = raw->links;
    v->uid = raw->uid | (uint32_t)load16(raw->os2 + 4) << 16;
    v->gid = raw->gid | (uint32_t)load16(raw->os2 + 6) << 16;
    v->size = raw->size;
    if (v->type == VFS_REG && node->fs->large_file) v->size |= (uint64_t)raw->size_high << 32;
    v->atime = raw->atime;
    v->mtime = raw->mtime;
    v->ctime = raw->ctime;
    if (v->type == VFS_CHR || v->type == VFS_BLK) {
        uint32_t old = raw->block[0];
        v->rdev = old ? old & 0xffff : raw->block[1] & 0xfffff;
    }
}

static int get_node(struct ext2_fs *fs, uint32_t ino, struct vnode **result) {
    struct ext2_node *node = cached(fs, ino);
    if (node) {
        *result = vnode_get(&node->vnode);
        return 0;
    }
    node = kzalloc(sizeof(*node));
    if (!node) return -NARC_NO_MEMORY;
    node->fs = fs;
    node->ino = ino;
    int status = read_inode(fs, ino, &node->raw);
    if (status) {
        kfree(node);
        return status;
    }
    fill_vnode(node);
    node->vnode.refs = 1;
    node->next = fs->nodes;
    fs->nodes = node;
    *result = &node->vnode;
    return 0;
}

static long read_data(struct ext2_node *node, uint64_t offset, void *buffer, size_t length) {
    struct ext2_fs *fs = node->fs;
    if (offset >= node->vnode.size) return 0;
    if (length > node->vnode.size - offset) length = (size_t)(node->vnode.size - offset);
    size_t done = 0;
    while (done < length) {
        uint64_t position = offset + done;
        uint32_t within = (uint32_t)(position % fs->block_size);
        size_t chunk = fs->block_size - within;
        if (chunk > length - done) chunk = length - done;
        uint32_t block = bmap(node, position / fs->block_size, 0);
        if (!block) {
            memset((uint8_t *)buffer + done, 0, chunk);
        } else {
            struct buffer *data = bread(fs->device, block, fs->block_size);
            if (!data) return done ? (long)done : -NARC_IO_ERROR;
            memcpy((uint8_t *)buffer + done, data->data + within, chunk);
            brelse(data);
        }
        done += chunk;
    }
    return (long)done;
}

static long write_data(struct ext2_node *node, uint64_t offset, const void *buffer,
                       size_t length) {
    struct ext2_fs *fs = node->fs;
    size_t done = 0;
    long status = 0;
    while (done < length) {
        uint64_t position = offset + done;
        uint32_t within = (uint32_t)(position % fs->block_size);
        size_t chunk = fs->block_size - within;
        if (chunk > length - done) chunk = length - done;
        uint32_t block = bmap(node, position / fs->block_size, 1);
        if (!block) {
            status = -NARC_NO_SPACE;
            break;
        }
        struct buffer *data = bread(fs->device, block, fs->block_size);
        if (!data) {
            status = -NARC_IO_ERROR;
            break;
        }
        memcpy(data->data + within, (const uint8_t *)buffer + done, chunk);
        status = bwrite(data);
        brelse(data);
        if (status) break;
        done += chunk;
    }
    if (offset + done > node->vnode.size) node->vnode.size = offset + done;
    if (done) node->vnode.mtime = node->vnode.ctime = now();
    write_inode(node);
    return done ? (long)done : status;
}

static uint8_t dirent_type(uint8_t type) {
    switch (type) {
    case VFS_REG: return 1;
    case VFS_DIR: return 2;
    case VFS_CHR: return 3;
    case VFS_BLK: return 4;
    case VFS_LNK: return 7;
    default: return 0;
    }
}

static size_t record_size(size_t name_length) {
    return (8 + name_length + 3) & ~(size_t)3;
}

struct dir_search {
    uint32_t block;
    uint32_t offset;
    uint32_t previous;
    int has_previous;
    uint32_t inode;
    uint8_t type;
};

static int find_entry(struct ext2_node *dir, const char *name, size_t length,
                      struct dir_search *search) {
    struct ext2_fs *fs = dir->fs;
    uint64_t blocks = (dir->vnode.size + fs->block_size - 1) / fs->block_size;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data) return -NARC_NO_MEMORY;
    for (uint64_t logical = 0; logical < blocks; logical++) {
        uint32_t block = bmap(dir, logical, 0);
        if (!block || read_block(fs, block, data) != 0) continue;
        uint32_t previous = 0;
        int has_previous = 0;
        for (uint32_t offset = 0; offset + 8 <= fs->block_size;) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(data + offset);
            if (entry->record_length < 8 || offset + entry->record_length > fs->block_size) break;
            if (entry->inode && entry->name_length == length && !memcmp(entry->name, name, length)) {
                search->block = block;
                search->offset = offset;
                search->previous = previous;
                search->has_previous = has_previous;
                search->inode = entry->inode;
                search->type = entry->type;
                kfree(data);
                return 0;
            }
            previous = offset;
            has_previous = 1;
            offset += entry->record_length;
        }
    }
    kfree(data);
    return -NARC_NOT_FOUND;
}

static void clear_index(struct ext2_node *dir) {
    if (dir->raw.flags & EXT2_INDEX_FLAG) dir->raw.flags &= ~EXT2_INDEX_FLAG;
}

static int add_entry(struct ext2_node *dir, const char *name, size_t length, uint32_t ino,
                     uint8_t type) {
    struct ext2_fs *fs = dir->fs;
    size_t needed = record_size(length);
    uint64_t blocks = (dir->vnode.size + fs->block_size - 1) / fs->block_size;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data) return -NARC_NO_MEMORY;
    clear_index(dir);
    for (uint64_t logical = 0; logical <= blocks; logical++) {
        uint32_t block = bmap(dir, logical, logical == blocks);
        if (!block) {
            kfree(data);
            return -NARC_NO_SPACE;
        }
        if (logical == blocks) {
            memset(data, 0, fs->block_size);
            struct ext2_dirent *entry = (struct ext2_dirent *)data;
            entry->record_length = (uint16_t)fs->block_size;
            dir->vnode.size += fs->block_size;
        } else if (read_block(fs, block, data) != 0) {
            continue;
        }
        for (uint32_t offset = 0; offset + 8 <= fs->block_size;) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(data + offset);
            if (entry->record_length < 8) break;
            size_t used = entry->inode ? record_size(entry->name_length) : 0;
            if (entry->record_length - used >= needed) {
                struct ext2_dirent *target = entry;
                if (used) {
                    target = (struct ext2_dirent *)(data + offset + used);
                    target->record_length = (uint16_t)(entry->record_length - used);
                    entry->record_length = (uint16_t)used;
                }
                target->inode = ino;
                target->name_length = (uint8_t)length;
                target->type = fs->filetype ? dirent_type(type) : 0;
                memcpy(target->name, name, length);
                int status = write_block(fs, block, data);
                kfree(data);
                dir->vnode.mtime = dir->vnode.ctime = now();
                write_inode(dir);
                return status;
            }
            offset += entry->record_length;
        }
    }
    kfree(data);
    return -NARC_NO_SPACE;
}

static int remove_entry(struct ext2_node *dir, const struct dir_search *search) {
    struct ext2_fs *fs = dir->fs;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data) return -NARC_NO_MEMORY;
    int status = read_block(fs, search->block, data);
    if (!status) {
        struct ext2_dirent *entry = (struct ext2_dirent *)(data + search->offset);
        if (search->has_previous) {
            struct ext2_dirent *previous = (struct ext2_dirent *)(data + search->previous);
            previous->record_length = (uint16_t)(previous->record_length + entry->record_length);
        } else {
            entry->inode = 0;
        }
        status = write_block(fs, search->block, data);
    }
    kfree(data);
    clear_index(dir);
    dir->vnode.mtime = dir->vnode.ctime = now();
    write_inode(dir);
    return status;
}

static int set_parent(struct ext2_node *dir, uint32_t parent) {
    struct dir_search search;
    int status = find_entry(dir, "..", 2, &search);
    if (status) return status;
    uint32_t value = parent;
    return update_bytes(dir->fs, search.block, search.offset, &value, 4);
}

static int directory_empty(struct ext2_node *dir) {
    struct ext2_fs *fs = dir->fs;
    uint64_t blocks = (dir->vnode.size + fs->block_size - 1) / fs->block_size;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data) return 0;
    int empty = 1;
    for (uint64_t logical = 0; logical < blocks && empty; logical++) {
        uint32_t block = bmap(dir, logical, 0);
        if (!block || read_block(fs, block, data) != 0) continue;
        for (uint32_t offset = 0; offset + 8 <= fs->block_size;) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(data + offset);
            if (entry->record_length < 8) break;
            int dot = entry->name_length == 1 && entry->name[0] == '.';
            int dotdot = entry->name_length == 2 && entry->name[0] == '.' && entry->name[1] == '.';
            if (entry->inode && !dot && !dotdot) {
                empty = 0;
                break;
            }
            offset += entry->record_length;
        }
    }
    kfree(data);
    return empty;
}

static void destroy_inode(struct ext2_node *node) {
    struct ext2_fs *fs = node->fs;
    int directory = node->vnode.type == VFS_DIR;
    if (!fast_symlink(node)) release_blocks(node, 0);
    memset(node->raw.block, 0, sizeof(node->raw.block));
    node->raw.dtime = now();
    node->raw.sectors = 0;
    node->vnode.size = 0;
    node->vnode.nlink = 0;
    write_inode(node);
    free_inode(fs, node->ino, directory);
}

static void release_locked(struct ext2_node *node) {
    struct ext2_fs *fs = node->fs;
    if (!node->vnode.nlink) destroy_inode(node);
    struct ext2_node **link = &fs->nodes;
    while (*link && *link != node) link = &(*link)->next;
    if (*link) *link = node->next;
    kfree(node);
}

static void put_locked(struct vnode *vnode) {
    if (__atomic_sub_fetch(&vnode->refs, 1, __ATOMIC_ACQ_REL)) return;
    release_locked(enode(vnode));
}

static int ext2_lookup(struct vnode *dir, const char *name, size_t length, struct vnode **result) {
    struct ext2_node *node = enode(dir);
    struct ext2_fs *fs = node->fs;
    mutex_lock(&fs->lock);
    struct dir_search search;
    int status = find_entry(node, name, length, &search);
    if (!status) status = get_node(fs, search.inode, result);
    mutex_unlock(&fs->lock);
    return status;
}

static int create_locked(struct ext2_node *parent, const char *name, size_t length, uint32_t mode,
                         uint32_t rdev, struct ext2_node **result) {
    struct ext2_fs *fs = parent->fs;
    struct dir_search search;
    if (find_entry(parent, name, length, &search) == 0) return -NARC_EXISTS;
    int directory = vfs_mode_type(mode) == VFS_DIR;
    uint32_t ino = allocate_inode(fs, directory);
    if (!ino) return -NARC_NO_SPACE;
    struct ext2_node *node = kzalloc(sizeof(*node));
    if (!node) {
        free_inode(fs, ino, directory);
        return -NARC_NO_MEMORY;
    }
    node->fs = fs;
    node->ino = ino;
    uint32_t stamp = now();
    node->raw.mode = (uint16_t)mode;
    node->raw.links = directory ? 2 : 1;
    node->raw.atime = node->raw.ctime = node->raw.mtime = stamp;
    if (vfs_mode_type(mode) == VFS_CHR || vfs_mode_type(mode) == VFS_BLK)
        node->raw.block[0] = rdev & 0xffff;
    fill_vnode(node);
    node->vnode.refs = 1;
    node->next = fs->nodes;
    fs->nodes = node;
    if (directory) {
        uint32_t block = bmap(node, 0, 1);
        uint8_t *data = block ? kzalloc(fs->block_size) : 0;
        if (data) {
            struct ext2_dirent *dot = (struct ext2_dirent *)data;
            dot->inode = ino;
            dot->record_length = 12;
            dot->name_length = 1;
            dot->type = fs->filetype ? 2 : 0;
            dot->name[0] = '.';
            struct ext2_dirent *dotdot = (struct ext2_dirent *)(data + 12);
            dotdot->inode = parent->ino;
            dotdot->record_length = (uint16_t)(fs->block_size - 12);
            dotdot->name_length = 2;
            dotdot->type = fs->filetype ? 2 : 0;
            dotdot->name[0] = dotdot->name[1] = '.';
            write_block(fs, block, data);
            kfree(data);
            node->vnode.size = fs->block_size;
        }
    }
    write_inode(node);
    int status = add_entry(parent, name, length, ino, node->vnode.type);
    if (status) {
        node->vnode.nlink = 0;
        put_locked(&node->vnode);
        return status;
    }
    if (directory) {
        parent->vnode.nlink++;
        write_inode(parent);
    }
    *result = node;
    return 0;
}

static int ext2_create(struct vnode *dir, const char *name, size_t length, uint32_t mode,
                       uint32_t rdev, struct vnode **result) {
    struct ext2_node *parent = enode(dir);
    struct ext2_fs *fs = parent->fs;
    mutex_lock(&fs->lock);
    struct ext2_node *node;
    int status = create_locked(parent, name, length, mode, rdev, &node);
    mutex_unlock(&fs->lock);
    if (status) return status;
    if (result) *result = &node->vnode;
    else vnode_put(&node->vnode);
    return 0;
}

static int unlink_locked(struct ext2_node *parent, const char *name, size_t length, int directory) {
    struct ext2_fs *fs = parent->fs;
    struct dir_search search;
    int status = find_entry(parent, name, length, &search);
    if (status) return status;
    struct vnode *vnode;
    status = get_node(fs, search.inode, &vnode);
    if (status) return status;
    struct ext2_node *node = enode(vnode);
    if (directory && vnode->type != VFS_DIR) status = -NARC_NOT_DIRECTORY;
    else if (!directory && vnode->type == VFS_DIR) status = -NARC_IS_DIRECTORY;
    else if (vnode->type == VFS_DIR && !directory_empty(node)) status = -NARC_NOT_EMPTY;
    if (!status) status = remove_entry(parent, &search);
    if (!status) {
        if (vnode->type == VFS_DIR) {
            vnode->nlink = 0;
            if (parent->vnode.nlink > 2) parent->vnode.nlink--;
            write_inode(parent);
        } else if (vnode->nlink) {
            vnode->nlink--;
        }
        vnode->ctime = now();
        write_inode(node);
    }
    put_locked(vnode);
    return status;
}

static int ext2_unlink(struct vnode *dir, const char *name, size_t length, int directory) {
    struct ext2_fs *fs = enode(dir)->fs;
    mutex_lock(&fs->lock);
    int status = unlink_locked(enode(dir), name, length, directory);
    mutex_unlock(&fs->lock);
    return status;
}

static int rename_locked(struct ext2_node *from, const char *old_name, size_t old_length,
                         struct ext2_node *to, const char *new_name, size_t new_length) {
    struct ext2_fs *fs = from->fs;
    struct dir_search source;
    int status = find_entry(from, old_name, old_length, &source);
    if (status) return status;
    struct vnode *moving;
    status = get_node(fs, source.inode, &moving);
    if (status) return status;
    struct dir_search existing;
    if (find_entry(to, new_name, new_length, &existing) == 0) {
        if (existing.inode == source.inode) {
            put_locked(moving);
            return 0;
        }
        struct vnode *victim;
        status = get_node(fs, existing.inode, &victim);
        if (!status) {
            if (moving->type == VFS_DIR && victim->type != VFS_DIR) status = -NARC_NOT_DIRECTORY;
            else if (moving->type != VFS_DIR && victim->type == VFS_DIR) status = -NARC_IS_DIRECTORY;
            put_locked(victim);
        }
        if (!status) status = unlink_locked(to, new_name, new_length, moving->type == VFS_DIR);
    }
    if (!status) status = add_entry(to, new_name, new_length, source.inode, moving->type);
    if (!status && find_entry(from, old_name, old_length, &source) == 0)
        status = remove_entry(from, &source);
    if (!status && moving->type == VFS_DIR && from != to) {
        set_parent(enode(moving), to->ino);
        from->vnode.nlink--;
        to->vnode.nlink++;
        write_inode(from);
        write_inode(to);
    }
    put_locked(moving);
    return status;
}

static int ext2_rename(struct vnode *old_dir, const char *old_name, size_t old_length,
                       struct vnode *new_dir, const char *new_name, size_t new_length) {
    struct ext2_fs *fs = enode(old_dir)->fs;
    mutex_lock(&fs->lock);
    int status = rename_locked(enode(old_dir), old_name, old_length, enode(new_dir), new_name,
                               new_length);
    mutex_unlock(&fs->lock);
    return status;
}

static int ext2_readdir(struct vnode *dir, uint64_t *cookie, struct vfs_dirent *out) {
    struct ext2_node *node = enode(dir);
    struct ext2_fs *fs = node->fs;
    mutex_lock(&fs->lock);
    int result = 0;
    uint8_t *data = kmalloc(fs->block_size);
    while (data && *cookie < dir->size) {
        uint32_t block = bmap(node, *cookie / fs->block_size, 0);
        uint32_t offset = (uint32_t)(*cookie % fs->block_size);
        if (!block || read_block(fs, block, data) != 0) {
            *cookie = (*cookie / fs->block_size + 1) * fs->block_size;
            continue;
        }
        struct ext2_dirent *entry = (struct ext2_dirent *)(data + offset);
        if (entry->record_length < 8 || offset + entry->record_length > fs->block_size) {
            *cookie = (*cookie / fs->block_size + 1) * fs->block_size;
            continue;
        }
        *cookie += entry->record_length;
        if (!entry->inode) continue;
        out->ino = entry->inode;
        memcpy(out->name, entry->name, entry->name_length);
        out->name[entry->name_length] = 0;
        switch (entry->type) {
        case 2: out->type = VFS_DIR; break;
        case 3: out->type = VFS_CHR; break;
        case 4: out->type = VFS_BLK; break;
        case 7: out->type = VFS_LNK; break;
        default: out->type = VFS_REG; break;
        }
        result = 1;
        break;
    }
    kfree(data);
    mutex_unlock(&fs->lock);
    return result;
}

static long ext2_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length) {
    struct ext2_node *node = enode(vnode);
    mutex_lock(&node->fs->lock);
    long result = read_data(node, offset, buffer, length);
    mutex_unlock(&node->fs->lock);
    return result;
}

static long ext2_write(struct vnode *vnode, uint64_t offset, const void *buffer, size_t length) {
    struct ext2_node *node = enode(vnode);
    if (!node->fs->large_file && offset + length > 0x7fffffffULL) return -NARC_TOO_BIG;
    mutex_lock(&node->fs->lock);
    long result = write_data(node, offset, buffer, length);
    mutex_unlock(&node->fs->lock);
    return result;
}

static int ext2_truncate(struct vnode *vnode, uint64_t size) {
    struct ext2_node *node = enode(vnode);
    struct ext2_fs *fs = node->fs;
    mutex_lock(&fs->lock);
    if (size < vnode->size) {
        uint64_t keep = (size + fs->block_size - 1) / fs->block_size;
        release_blocks(node, keep);
        uint32_t tail = (uint32_t)(size % fs->block_size);
        uint32_t block = tail ? bmap(node, size / fs->block_size, 0) : 0;
        if (block) {
            struct buffer *data = bread(fs->device, block, fs->block_size);
            if (data) {
                memset(data->data + tail, 0, fs->block_size - tail);
                bwrite(data);
                brelse(data);
            }
        }
    }
    vnode->size = size;
    vnode->mtime = vnode->ctime = now();
    int status = write_inode(node);
    mutex_unlock(&fs->lock);
    return status;
}

static int ext2_symlink(struct vnode *dir, const char *name, size_t length, const char *target) {
    struct ext2_node *parent = enode(dir);
    struct ext2_fs *fs = parent->fs;
    size_t target_length = strlen(target);
    if (!target_length || target_length >= fs->block_size) return -NARC_NAME_TOO_LONG;
    mutex_lock(&fs->lock);
    struct ext2_node *node;
    int status = create_locked(parent, name, length, S_IFLNK | 0777, 0, &node);
    if (!status) {
        if (target_length < FAST_SYMLINK_MAX) {
            memcpy(node->raw.block, target, target_length);
            node->vnode.size = target_length;
            write_inode(node);
        } else {
            long wrote = write_data(node, 0, target, target_length);
            if (wrote < 0) status = (int)wrote;
        }
        put_locked(&node->vnode);
    }
    mutex_unlock(&fs->lock);
    return status;
}

static long ext2_readlink(struct vnode *vnode, char *buffer, size_t length) {
    struct ext2_node *node = enode(vnode);
    size_t size = vnode->size < length ? (size_t)vnode->size : length;
    if (fast_symlink(node)) {
        memcpy(buffer, node->raw.block, size);
        return (long)size;
    }
    mutex_lock(&node->fs->lock);
    long result = read_data(node, 0, buffer, size);
    mutex_unlock(&node->fs->lock);
    return result;
}

static int ext2_link(struct vnode *dir, const char *name, size_t length, struct vnode *target) {
    struct ext2_node *parent = enode(dir);
    struct ext2_fs *fs = parent->fs;
    mutex_lock(&fs->lock);
    struct dir_search search;
    int status = find_entry(parent, name, length, &search) == 0 ? -NARC_EXISTS :
                 add_entry(parent, name, length, enode(target)->ino, target->type);
    if (!status) {
        target->nlink++;
        target->ctime = now();
        write_inode(enode(target));
    }
    mutex_unlock(&fs->lock);
    return status;
}

static int ext2_write_attr(struct vnode *vnode) {
    struct ext2_node *node = enode(vnode);
    mutex_lock(&node->fs->lock);
    int status = write_inode(node);
    mutex_unlock(&node->fs->lock);
    return status;
}

static void ext2_release(struct vnode *vnode) {
    struct ext2_node *node = enode(vnode);
    struct ext2_fs *fs = node->fs;
    mutex_lock(&fs->lock);
    if (__atomic_load_n(&vnode->refs, __ATOMIC_ACQUIRE)) {
        mutex_unlock(&fs->lock);
        return;
    }
    release_locked(node);
    int finished = fs->unmounting && !fs->nodes;
    mutex_unlock(&fs->lock);
    if (finished) {
        kfree(fs->groups);
        kfree(fs);
    }
}

static const struct vnode_ops ext2_ops = {
    .lookup = ext2_lookup,
    .create = ext2_create,
    .unlink = ext2_unlink,
    .rename = ext2_rename,
    .readdir = ext2_readdir,
    .read = ext2_read,
    .write = ext2_write,
    .truncate = ext2_truncate,
    .symlink = ext2_symlink,
    .readlink = ext2_readlink,
    .link = ext2_link,
    .write_attr = ext2_write_attr,
    .release = ext2_release,
};

static int ext2_mount(struct mount *mount, struct vnode *device_node) {
    struct block_device *device = block_find(device_node->rdev);
    if (!device) return -NARC_NO_DEVICE;
    struct ext2_fs *fs = kzalloc(sizeof(*fs));
    if (!fs) return -NARC_NO_MEMORY;
    fs->device = device;
    fs->mount = mount;
    mutex_init(&fs->lock);
    int status = block_io(device, 2, 2, &fs->super, 0);
    if (!status && fs->super.magic != EXT2_MAGIC) status = -NARC_INVALID_ARGUMENT;
    if (!status && (fs->super.incompat & ~(uint32_t)INCOMPAT_FILETYPE)) status = -NARC_NOT_SUPPORTED;
    if (!status && fs->super.log_block_size > 2) status = -NARC_NOT_SUPPORTED;
    if (status) {
        kfree(fs);
        return status;
    }
    fs->block_size = 1024u << fs->super.log_block_size;
    fs->pointers = fs->block_size / 4;
    fs->inode_size = fs->super.revision ? fs->super.inode_size : 128;
    fs->filetype = (fs->super.incompat & INCOMPAT_FILETYPE) != 0;
    fs->large_file = (fs->super.ro_compat & RO_COMPAT_LARGE_FILE) != 0;
    if (fs->super.ro_compat & ~(uint32_t)(RO_COMPAT_SPARSE | RO_COMPAT_LARGE_FILE))
        mount->flags |= VFS_MOUNT_READONLY;
    fs->group_count = (fs->super.blocks_count - fs->super.first_data_block +
                       fs->super.blocks_per_group - 1) / fs->super.blocks_per_group;
    size_t table = (size_t)fs->group_count * sizeof(struct ext2_group);
    size_t table_blocks = (table + fs->block_size - 1) / fs->block_size;
    fs->groups = kmalloc(table_blocks * fs->block_size);
    for (size_t i = 0; fs->groups && i < table_blocks && !status; i++)
        status = read_block(fs, fs->super.first_data_block + 1 + (uint32_t)i,
                            (uint8_t *)fs->groups + i * fs->block_size);
    if (!fs->groups) status = -NARC_NO_MEMORY;
    struct vnode *root = 0;
    if (!status) {
        mount->data = fs;
        status = get_node(fs, EXT2_ROOT, &root);
    }
    if (status) {
        kfree(fs->groups);
        kfree(fs);
        return status;
    }
    mount->root = root;
    if (!(mount->flags & VFS_MOUNT_READONLY)) {
        fs->super.mount_time = now();
        fs->super.mount_count++;
        write_super(fs);
    }
    return 0;
}

static int ext2_unmount(struct mount *mount) {
    struct ext2_fs *fs = mount->data;
    for (struct ext2_node *node = fs->nodes; node; node = node->next)
        if (&node->vnode != mount->root && node->vnode.refs) return -NARC_BUSY;
    if (!(mount->flags & VFS_MOUNT_READONLY)) write_super(fs);
    fs->unmounting = 1;
    return 0;
}

static int ext2_statfs(struct mount *mount, struct vfs_statfs *info) {
    struct ext2_fs *fs = mount->data;
    mutex_lock(&fs->lock);
    uint32_t overhead = fs->super.first_data_block;
    info->block_size = fs->block_size;
    info->blocks = fs->super.blocks_count - overhead;
    info->free_blocks = fs->super.free_blocks;
    info->available_blocks = fs->super.free_blocks > fs->super.reserved_blocks ?
                             fs->super.free_blocks - fs->super.reserved_blocks : 0;
    info->files = fs->super.inodes_count;
    info->free_files = fs->super.free_inodes;
    info->name_max = EXT2_NAME_MAX;
    mutex_unlock(&fs->lock);
    return 0;
}

struct filesystem ext2_filesystem = {
    .name = "ext2",
    .requires_device = 1,
    .mount = ext2_mount,
    .unmount = ext2_unmount,
    .statfs = ext2_statfs,
};
