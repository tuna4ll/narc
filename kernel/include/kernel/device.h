#pragma once
#include <kernel/file.h>
#include <stdint.h>

#define DEVICE_MAKE(major, minor) (((uint32_t)(major) << 8) | (uint32_t)(minor))
#define DEVICE_MAJOR(rdev) ((rdev) >> 8)
#define DEVICE_MINOR(rdev) ((rdev) & 0xff)

struct char_device {
    struct char_device *next;
    uint32_t rdev;
    const char *name;
    int (*open)(struct char_device *device, uint32_t flags, struct open_file **result);
    void *data;
};

void device_register(struct char_device *device);
int device_open(struct vnode *node, uint32_t flags, struct open_file **result);
void devfs_init(void);
void memory_devices_init(void);
