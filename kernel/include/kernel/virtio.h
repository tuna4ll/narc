#pragma once
#include <stddef.h>
#include <stdint.h>

#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER      2u
#define VIRTIO_STATUS_DRIVER_OK   4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED      128u
#define VIRTIO_F_VERSION_1        (1ULL << 32)
#define VIRTIO_DEVICE_BLOCK       2
#define VIRTIO_DEVICE_INPUT       18
#define VIRTQ_DESC_NEXT           1u
#define VIRTQ_DESC_WRITE          2u

struct virtio_device;

struct virtio_transport {
    uint8_t (*get_status)(struct virtio_device *device);
    void (*set_status)(struct virtio_device *device, uint8_t status);
    uint64_t (*get_features)(struct virtio_device *device);
    void (*set_features)(struct virtio_device *device, uint64_t features);
    uint16_t (*queue_max)(struct virtio_device *device, uint16_t index);
    int (*queue_setup)(struct virtio_device *device, uint16_t index, uint16_t size,
                       uint64_t desc, uint64_t driver, uint64_t used);
    void (*notify)(struct virtio_device *device, uint16_t index);
    void (*read_config)(struct virtio_device *device, size_t offset, void *buffer, size_t length);
};

struct virtq_desc {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[];
};

struct virtq_used_element {
    uint32_t id;
    uint32_t length;
};

struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_element ring[];
};

struct virtqueue {
    uint16_t index, size, last_used;
    uint64_t page;
    volatile struct virtq_desc *desc;
    volatile struct virtq_avail *avail;
    volatile struct virtq_used *used;
};

struct virtio_segment {
    uint64_t address;
    uint32_t length;
    int device_writes;
};

struct virtio_device {
    const struct virtio_transport *transport;
    void *data;
    uint64_t features;
};

int virtio_initialize(struct virtio_device *device, uint64_t wanted_features);
int virtio_queue_create(struct virtio_device *device, struct virtqueue *queue, uint16_t index);
void virtio_ready(struct virtio_device *device);
int virtio_transfer(struct virtio_device *device, struct virtqueue *queue,
                    const struct virtio_segment *segments, unsigned count);
int virtio_probe(struct virtio_device *device, uint32_t type);
void virtio_mmio_probe(uint64_t base, uint64_t stride, unsigned count);
int virtio_block_attach(struct virtio_device *device);
int virtio_input_attach(struct virtio_device *device);
void *virtio_map(uint64_t phys, uint64_t length);
