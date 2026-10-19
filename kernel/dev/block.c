#include <kernel/block.h>
#include <kernel/device.h>
#include <kernel/heap.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <narc/abi.h>

#define CACHE_BUFFERS 512

static struct block_device *devices;
static unsigned device_count;
static struct buffer *lru_head, *lru_tail;
static unsigned buffer_count;
static struct mutex cache_lock = MUTEX_INIT;

void block_register(struct block_device *device) {
    device->rdev = DEVICE_MAKE(BLOCK_MAJOR, device_count);
    device->name[0] = 'v';
    device->name[1] = 'd';
    device->name[2] = (char)('a' + device_count);
    device->name[3] = 0;
    device_count++;
    mutex_init(&device->lock);
    struct block_device **link = &devices;
    while (*link) link = &(*link)->next;
    *link = device;
}

struct block_device *block_find(uint32_t rdev) {
    for (struct block_device *device = devices; device; device = device->next)
        if (device->rdev == rdev) return device;
    return 0;
}

void block_publish(void) {
    for (struct block_device *device = devices; device; device = device->next) {
        char path[32] = "/dev/";
        memcpy(path + 5, device->name, strlen(device->name) + 1);
        vfs_mknod(vfs_root(), path, S_IFBLK | 0660, device->rdev);
    }
}

int block_io(struct block_device *device, uint64_t sector, uint32_t count, void *buffer, int write) {
    if (sector > device->sectors || count > device->sectors - sector) return -NARC_INVALID_ARGUMENT;
    mutex_lock(&device->lock);
    int status = write ? device->write(device, sector, count, buffer) :
                         device->read(device, sector, count, buffer);
    mutex_unlock(&device->lock);
    return status ? -NARC_IO_ERROR : 0;
}

static void lru_unlink(struct buffer *buffer) {
    if (buffer->previous) buffer->previous->next = buffer->next;
    else lru_head = buffer->next;
    if (buffer->next) buffer->next->previous = buffer->previous;
    else lru_tail = buffer->previous;
    buffer->next = buffer->previous = 0;
}

static void lru_push(struct buffer *buffer) {
    buffer->previous = 0;
    buffer->next = lru_head;
    if (lru_head) lru_head->previous = buffer;
    lru_head = buffer;
    if (!lru_tail) lru_tail = buffer;
}

static struct buffer *evict(uint32_t size) {
    for (struct buffer *buffer = lru_tail; buffer; buffer = buffer->previous) {
        if (buffer->refs) continue;
        lru_unlink(buffer);
        if (buffer->size != size) {
            uint8_t *data = kmalloc(size);
            if (!data) return 0;
            kfree(buffer->data);
            buffer->data = data;
            buffer->size = size;
        }
        return buffer;
    }
    return 0;
}

static struct buffer *acquire(struct block_device *device, uint64_t block, uint32_t size) {
    for (struct buffer *buffer = lru_head; buffer; buffer = buffer->next) {
        if (buffer->device != device || buffer->block != block || buffer->size != size) continue;
        lru_unlink(buffer);
        lru_push(buffer);
        buffer->refs++;
        return buffer;
    }
    struct buffer *buffer = 0;
    if (buffer_count >= CACHE_BUFFERS) buffer = evict(size);
    if (!buffer) {
        buffer = kzalloc(sizeof(*buffer));
        if (!buffer) return 0;
        buffer->data = kmalloc(size);
        if (!buffer->data) {
            kfree(buffer);
            return 0;
        }
        buffer->size = size;
        buffer_count++;
    }
    buffer->device = device;
    buffer->block = block;
    buffer->valid = 0;
    buffer->refs = 1;
    lru_push(buffer);
    return buffer;
}

struct buffer *bread(struct block_device *device, uint64_t block, uint32_t size) {
    mutex_lock(&cache_lock);
    struct buffer *buffer = acquire(device, block, size);
    if (buffer && !buffer->valid) {
        uint32_t sectors = size / SECTOR_SIZE;
        if (block_io(device, block * sectors, sectors, buffer->data, 0) == 0) {
            buffer->valid = 1;
        } else {
            buffer->refs--;
            buffer = 0;
        }
    }
    mutex_unlock(&cache_lock);
    return buffer;
}

int bwrite(struct buffer *buffer) {
    uint32_t sectors = buffer->size / SECTOR_SIZE;
    return block_io(buffer->device, buffer->block * sectors, sectors, buffer->data, 1);
}

void brelse(struct buffer *buffer) {
    if (!buffer) return;
    mutex_lock(&cache_lock);
    if (buffer->refs) buffer->refs--;
    mutex_unlock(&cache_lock);
}

void binvalidate(struct block_device *device) {
    mutex_lock(&cache_lock);
    for (struct buffer *buffer = lru_head; buffer; buffer = buffer->next)
        if (buffer->device == device && !buffer->refs) buffer->valid = 0;
    mutex_unlock(&cache_lock);
}

static long raw_transfer(struct open_file *open, void *data, size_t length, int write) {
    struct block_device *device = open->private;
    uint8_t *bounce = kmalloc(SECTOR_SIZE);
    if (!bounce) return -NARC_NO_MEMORY;
    mutex_lock(&open->lock);
    uint64_t total = device->sectors * SECTOR_SIZE;
    size_t done = 0;
    long status = 0;
    while (done < length && open->offset < total) {
        uint64_t sector = open->offset / SECTOR_SIZE;
        size_t within = (size_t)(open->offset % SECTOR_SIZE);
        size_t chunk = SECTOR_SIZE - within;
        if (chunk > length - done) chunk = length - done;
        status = block_io(device, sector, 1, bounce, 0);
        if (status) break;
        if (write) {
            memcpy(bounce + within, (const uint8_t *)data + done, chunk);
            status = block_io(device, sector, 1, bounce, 1);
            if (status) break;
        } else {
            memcpy((uint8_t *)data + done, bounce + within, chunk);
        }
        done += chunk;
        open->offset += chunk;
    }
    mutex_unlock(&open->lock);
    kfree(bounce);
    if (write && done) binvalidate(device);
    return done ? (long)done : status;
}

static long raw_read(struct open_file *open, void *buffer, size_t length) {
    return raw_transfer(open, buffer, length, 0);
}

static long raw_write(struct open_file *open, const void *buffer, size_t length) {
    return raw_transfer(open, (void *)buffer, length, 1);
}

static const struct file_ops raw_ops = { .read = raw_read, .write = raw_write };

int block_open(struct block_device *device, uint32_t flags, struct open_file **result) {
    *result = file_alloc(&raw_ops, device);
    if (!*result) return -NARC_NO_MEMORY;
    (*result)->flags = flags & VFS_STATUS_FLAGS;
    return 0;
}
