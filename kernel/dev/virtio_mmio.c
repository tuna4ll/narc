#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/virtio.h>

#define MMIO_MAGIC 0x000
#define MMIO_VERSION 0x004
#define MMIO_DEVICE_ID 0x008
#define MMIO_DEVICE_FEATURES 0x010
#define MMIO_DEVICE_FEATURES_SELECT 0x014
#define MMIO_DRIVER_FEATURES 0x020
#define MMIO_DRIVER_FEATURES_SELECT 0x024
#define MMIO_QUEUE_SELECT 0x030
#define MMIO_QUEUE_MAX 0x034
#define MMIO_QUEUE_SIZE 0x038
#define MMIO_QUEUE_READY 0x044
#define MMIO_QUEUE_NOTIFY 0x050
#define MMIO_STATUS 0x070
#define MMIO_QUEUE_DESC 0x080
#define MMIO_QUEUE_DRIVER 0x090
#define MMIO_QUEUE_DEVICE 0x0a0
#define MMIO_CONFIG 0x100

static volatile uint32_t *reg(struct virtio_device *device, size_t offset) {
    return (volatile uint32_t *)((uint8_t *)device->data + offset);
}

static uint8_t get_status(struct virtio_device *device) {
    return (uint8_t)*reg(device, MMIO_STATUS);
}

static void set_status(struct virtio_device *device, uint8_t status) {
    *reg(device, MMIO_STATUS) = status;
}

static uint64_t get_features(struct virtio_device *device) {
    *reg(device, MMIO_DEVICE_FEATURES_SELECT) = 0;
    uint64_t low = *reg(device, MMIO_DEVICE_FEATURES);
    *reg(device, MMIO_DEVICE_FEATURES_SELECT) = 1;
    uint64_t high = *reg(device, MMIO_DEVICE_FEATURES);
    return low | (high << 32);
}

static void set_features(struct virtio_device *device, uint64_t features) {
    *reg(device, MMIO_DRIVER_FEATURES_SELECT) = 0;
    *reg(device, MMIO_DRIVER_FEATURES) = (uint32_t)features;
    *reg(device, MMIO_DRIVER_FEATURES_SELECT) = 1;
    *reg(device, MMIO_DRIVER_FEATURES) = (uint32_t)(features >> 32);
}

static uint16_t queue_max(struct virtio_device *device, uint16_t index) {
    *reg(device, MMIO_QUEUE_SELECT) = index;
    return (uint16_t)*reg(device, MMIO_QUEUE_MAX);
}

static void write64(struct virtio_device *device, size_t offset, uint64_t value) {
    *reg(device, offset) = (uint32_t)value;
    *reg(device, offset + 4) = (uint32_t)(value >> 32);
}

static int queue_setup(struct virtio_device *device, uint16_t index, uint16_t size, uint64_t desc,
                       uint64_t driver, uint64_t used) {
    *reg(device, MMIO_QUEUE_SELECT) = index;
    *reg(device, MMIO_QUEUE_SIZE) = size;
    write64(device, MMIO_QUEUE_DESC, desc);
    write64(device, MMIO_QUEUE_DRIVER, driver);
    write64(device, MMIO_QUEUE_DEVICE, used);
    *reg(device, MMIO_QUEUE_READY) = 1;
    return 0;
}

static void notify(struct virtio_device *device, uint16_t index) {
    *reg(device, MMIO_QUEUE_NOTIFY) = index;
}

static void read_config(struct virtio_device *device, size_t offset, void *buffer, size_t length) {
    volatile uint8_t *config = (volatile uint8_t *)device->data + MMIO_CONFIG + offset;
    for (size_t i = 0; i < length; i++) ((uint8_t *)buffer)[i] = config[i];
}

static const struct virtio_transport mmio_transport = {
    get_status, set_status, get_features, set_features, queue_max, queue_setup, notify, read_config,
};

void virtio_mmio_probe(uint64_t base, uint64_t stride, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        volatile uint32_t *window = virtio_map(base + i * stride, stride);
        if (window[MMIO_MAGIC / 4] != 0x74726976 || window[MMIO_VERSION / 4] != 2) continue;
        uint32_t type = window[MMIO_DEVICE_ID / 4];
        if (!type) continue;
        struct virtio_device *device = kzalloc(sizeof(*device));
        if (!device) return;
        device->transport = &mmio_transport;
        device->data = (void *)window;
        if (virtio_probe(device, type) != 0) kfree(device);
    }
}
