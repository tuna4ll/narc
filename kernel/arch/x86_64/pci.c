#include <kernel/heap.h>
#include <kernel/io.h>
#include <kernel/mm.h>
#include <kernel/virtio.h>
#include <stdint.h>

#define PCI_ADDRESS 0xcf8
#define PCI_DATA 0xcfc
#define VIRTIO_VENDOR 0x1af4
#define CAP_COMMON 1
#define CAP_NOTIFY 2
#define CAP_DEVICE 4

struct pci_function {
    uint8_t bus, slot, function;
};

struct virtio_pci {
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *config;
    uint32_t notify_multiplier;
};

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t config_read(struct pci_function f, uint8_t offset) {
    outl(PCI_ADDRESS, 0x80000000u | ((uint32_t)f.bus << 16) | ((uint32_t)f.slot << 11) |
                      ((uint32_t)f.function << 8) | (offset & 0xfc));
    return inl(PCI_DATA);
}

static void config_write(struct pci_function f, uint8_t offset, uint32_t value) {
    outl(PCI_ADDRESS, 0x80000000u | ((uint32_t)f.bus << 16) | ((uint32_t)f.slot << 11) |
                      ((uint32_t)f.function << 8) | (offset & 0xfc));
    outl(PCI_DATA, value);
}

static uint64_t bar_address(struct pci_function f, uint8_t bar) {
    uint32_t low = config_read(f, (uint8_t)(0x10 + bar * 4));
    if (low & 1) return 0;
    uint64_t address = low & ~0xfULL;
    if (((low >> 1) & 3) == 2) address |= (uint64_t)config_read(f, (uint8_t)(0x14 + bar * 4)) << 32;
    return address;
}

static struct virtio_pci *device_pci(struct virtio_device *device) {
    return device->data;
}

static uint8_t get_status(struct virtio_device *device) {
    return device_pci(device)->common[0x14];
}

static void set_status(struct virtio_device *device, uint8_t status) {
    device_pci(device)->common[0x14] = status;
}

static volatile uint32_t *common32(struct virtio_device *device, size_t offset) {
    return (volatile uint32_t *)(device_pci(device)->common + offset);
}

static volatile uint16_t *common16(struct virtio_device *device, size_t offset) {
    return (volatile uint16_t *)(device_pci(device)->common + offset);
}

static uint64_t get_features(struct virtio_device *device) {
    *common32(device, 0x00) = 0;
    uint64_t low = *common32(device, 0x04);
    *common32(device, 0x00) = 1;
    uint64_t high = *common32(device, 0x04);
    return low | (high << 32);
}

static void set_features(struct virtio_device *device, uint64_t features) {
    *common32(device, 0x08) = 0;
    *common32(device, 0x0c) = (uint32_t)features;
    *common32(device, 0x08) = 1;
    *common32(device, 0x0c) = (uint32_t)(features >> 32);
}

static uint16_t queue_max(struct virtio_device *device, uint16_t index) {
    *common16(device, 0x16) = index;
    return *common16(device, 0x18);
}

static void write64(struct virtio_device *device, size_t offset, uint64_t value) {
    *common32(device, offset) = (uint32_t)value;
    *common32(device, offset + 4) = (uint32_t)(value >> 32);
}

static int queue_setup(struct virtio_device *device, uint16_t index, uint16_t size, uint64_t desc,
                       uint64_t driver, uint64_t used) {
    *common16(device, 0x16) = index;
    *common16(device, 0x18) = size;
    write64(device, 0x20, desc);
    write64(device, 0x28, driver);
    write64(device, 0x30, used);
    *common16(device, 0x1c) = 1;
    return 0;
}

static void notify(struct virtio_device *device, uint16_t index) {
    struct virtio_pci *pci = device_pci(device);
    *common16(device, 0x16) = index;
    uint16_t offset = *common16(device, 0x1e);
    *(volatile uint16_t *)(pci->notify + (size_t)offset * pci->notify_multiplier) = index;
}

static void read_config(struct virtio_device *device, size_t offset, void *buffer, size_t length) {
    volatile uint8_t *config = device_pci(device)->config + offset;
    for (size_t i = 0; i < length; i++) ((uint8_t *)buffer)[i] = config[i];
}

static const struct virtio_transport pci_transport = {
    get_status, set_status, get_features, set_features, queue_max, queue_setup, notify, read_config,
};

static void attach(struct pci_function f, uint32_t type) {
    struct virtio_pci *pci = kzalloc(sizeof(*pci));
    if (!pci) return;
    uint8_t cap = (uint8_t)(config_read(f, 0x34) & 0xfc);
    while (cap) {
        uint32_t header = config_read(f, cap);
        if ((header & 0xff) == 0x09) {
            uint8_t kind = (uint8_t)(header >> 24);
            uint8_t bar = (uint8_t)(config_read(f, (uint8_t)(cap + 4)) & 0xff);
            uint32_t offset = config_read(f, (uint8_t)(cap + 8));
            uint32_t length = config_read(f, (uint8_t)(cap + 12));
            uint64_t base = bar < 6 ? bar_address(f, bar) : 0;
            if (base) {
                volatile uint8_t *window = virtio_map(base + offset, length ? length : 4);
                if (kind == CAP_COMMON) pci->common = window;
                else if (kind == CAP_DEVICE) pci->config = window;
                else if (kind == CAP_NOTIFY) {
                    pci->notify = window;
                    pci->notify_multiplier = config_read(f, (uint8_t)(cap + 16));
                }
            }
        }
        cap = (uint8_t)((header >> 8) & 0xfc);
    }
    if (!pci->common || !pci->notify || !pci->config) {
        kfree(pci);
        return;
    }
    config_write(f, 0x04, config_read(f, 0x04) | 0x6);
    struct virtio_device *device = kzalloc(sizeof(*device));
    if (!device) {
        kfree(pci);
        return;
    }
    device->transport = &pci_transport;
    device->data = pci;
    if (virtio_probe(device, type) != 0) {
        kfree(device);
        kfree(pci);
    }
}

void pci_probe(void) {
    for (unsigned bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci_function f = { (uint8_t)bus, slot, function };
                uint32_t id = config_read(f, 0);
                if ((id & 0xffff) == 0xffff) {
                    if (!function) break;
                    continue;
                }
                uint16_t device_id = (uint16_t)(id >> 16);
                if ((id & 0xffff) == VIRTIO_VENDOR && device_id >= 0x1000 && device_id <= 0x107f) {
                    uint32_t type = device_id >= 0x1040 ? device_id - 0x1040u :
                                    (config_read(f, 0x2c) >> 16);
                    attach(f, type);
                }
                if (!function && !(config_read(f, 0x0c) & 0x00800000)) break;
            }
        }
    }
}

void arch_probe_devices(void) {
    pci_probe();
}
