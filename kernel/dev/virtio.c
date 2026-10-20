#include <kernel/arch.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/virtio.h>

#define QUEUE_LIMIT 64
#define TIMEOUT_SPINS 200000000ULL

void *virtio_map(uint64_t phys, uint64_t length) {
    uint64_t start = phys & ~(PAGE_SIZE - 1);
    for (uint64_t page = start; page < phys + length; page += PAGE_SIZE)
        vmm_map_kernel((uint64_t)(uintptr_t)phys_to_virt(page), page, VMM_WRITE | VMM_DEVICE);
    return phys_to_virt(phys);
}

int virtio_initialize(struct virtio_device *device, uint64_t wanted_features) {
    const struct virtio_transport *t = device->transport;
    t->set_status(device, 0);
    t->set_status(device, VIRTIO_STATUS_ACKNOWLEDGE);
    t->set_status(device, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    uint64_t offered = t->get_features(device);
    if (!(offered & VIRTIO_F_VERSION_1)) {
        t->set_status(device, VIRTIO_STATUS_FAILED);
        return -1;
    }
    device->features = offered & (wanted_features | VIRTIO_F_VERSION_1);
    t->set_features(device, device->features);
    uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    t->set_status(device, status);
    if (!(t->get_status(device) & VIRTIO_STATUS_FEATURES_OK)) {
        t->set_status(device, VIRTIO_STATUS_FAILED);
        return -1;
    }
    return 0;
}

int virtio_queue_create(struct virtio_device *device, struct virtqueue *queue, uint16_t index) {
    uint16_t size = device->transport->queue_max(device, index);
    if (!size) return -1;
    if (size > QUEUE_LIMIT) size = QUEUE_LIMIT;
    uint64_t page = pmm_alloc_page();
    if (!page) return -1;
    uint8_t *memory = phys_to_virt(page);
    size_t avail_offset = sizeof(struct virtq_desc) * size;
    size_t used_offset = (avail_offset + sizeof(struct virtq_avail) + 2 * size + 2 + 3) & ~(size_t)3;
    queue->index = index;
    queue->size = size;
    queue->last_used = 0;
    queue->page = page;
    queue->desc = (volatile struct virtq_desc *)memory;
    queue->avail = (volatile struct virtq_avail *)(memory + avail_offset);
    queue->used = (volatile struct virtq_used *)(memory + used_offset);
    return device->transport->queue_setup(device, index, size, page, page + avail_offset,
                                          page + used_offset);
}

void virtio_ready(struct virtio_device *device) {
    uint8_t status = device->transport->get_status(device);
    device->transport->set_status(device, status | VIRTIO_STATUS_DRIVER_OK);
}

int virtio_transfer(struct virtio_device *device, struct virtqueue *queue,
                    const struct virtio_segment *segments, unsigned count) {
    if (!count || count > queue->size) return -1;
    for (unsigned i = 0; i < count; i++) {
        queue->desc[i].address = segments[i].address;
        queue->desc[i].length = segments[i].length;
        queue->desc[i].flags = (uint16_t)((i + 1 < count ? VIRTQ_DESC_NEXT : 0) |
                                          (segments[i].device_writes ? VIRTQ_DESC_WRITE : 0));
        queue->desc[i].next = (uint16_t)(i + 1);
    }
    uint16_t slot = queue->avail->index;
    queue->avail->ring[slot % queue->size] = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    queue->avail->index = (uint16_t)(slot + 1);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    device->transport->notify(device, queue->index);
    for (uint64_t spins = 0; queue->used->index == queue->last_used; spins++) {
        if (spins > TIMEOUT_SPINS) return -1;
        arch_cpu_relax();
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    queue->last_used++;
    return 0;
}

int virtio_probe(struct virtio_device *device, uint32_t type) {
    switch (type) {
    case VIRTIO_DEVICE_BLOCK: return virtio_block_attach(device);
    case VIRTIO_DEVICE_INPUT: return virtio_input_attach(device);
    }
    return -1;
}
