#include <kernel/block.h>
#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/virtio.h>

#define REQUEST_IN 0
#define REQUEST_OUT 1
#define SECTORS_PER_PAGE (PAGE_SIZE / SECTOR_SIZE)

struct request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct virtio_block {
    struct block_device block;
    struct virtio_device *device;
    struct virtqueue queue;
    uint64_t header_page, data_page;
};

static int transfer(struct virtio_block *disk, uint64_t sector, uint32_t count, int write) {
    uint8_t *header_memory = phys_to_virt(disk->header_page);
    struct request_header *header = (struct request_header *)header_memory;
    volatile uint8_t *status = header_memory + 64;
    header->type = write ? REQUEST_OUT : REQUEST_IN;
    header->reserved = 0;
    header->sector = sector;
    *status = 0xff;
    struct virtio_segment segments[3] = {
        { disk->header_page, sizeof(*header), 0 },
        { disk->data_page, count * SECTOR_SIZE, !write },
        { disk->header_page + 64, 1, 1 },
    };
    if (virtio_transfer(disk->device, &disk->queue, segments, 3) != 0) return -1;
    return *status == 0 ? 0 : -1;
}

static int disk_io(struct block_device *block, uint64_t sector, uint32_t count, void *buffer,
                   int write) {
    struct virtio_block *disk = block->data;
    uint8_t *data = phys_to_virt(disk->data_page);
    uint8_t *cursor = buffer;
    while (count) {
        uint32_t chunk = count > SECTORS_PER_PAGE ? SECTORS_PER_PAGE : count;
        if (write) memcpy(data, cursor, chunk * SECTOR_SIZE);
        if (transfer(disk, sector, chunk, write) != 0) return -1;
        if (!write) memcpy(cursor, data, chunk * SECTOR_SIZE);
        cursor += chunk * SECTOR_SIZE;
        sector += chunk;
        count -= chunk;
    }
    return 0;
}

static int disk_read(struct block_device *block, uint64_t sector, uint32_t count, void *buffer) {
    return disk_io(block, sector, count, buffer, 0);
}

static int disk_write(struct block_device *block, uint64_t sector, uint32_t count,
                      const void *buffer) {
    return disk_io(block, sector, count, (void *)buffer, 1);
}

int virtio_block_attach(struct virtio_device *device) {
    struct virtio_block *disk = kzalloc(sizeof(*disk));
    if (!disk) return -1;
    disk->device = device;
    disk->header_page = pmm_alloc_page();
    disk->data_page = pmm_alloc_page();
    if (!disk->header_page || !disk->data_page || virtio_initialize(device, 0) != 0 ||
        virtio_queue_create(device, &disk->queue, 0) != 0) {
        kfree(disk);
        return -1;
    }
    virtio_ready(device);
    uint64_t capacity;
    device->transport->read_config(device, 0, &capacity, sizeof(capacity));
    disk->block.sectors = capacity;
    disk->block.read = disk_read;
    disk->block.write = disk_write;
    disk->block.data = disk;
    block_register(&disk->block);
    return 0;
}
