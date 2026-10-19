#include <kernel/block.h>
#include <kernel/device.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <narc/abi.h>

static struct char_device *devices;

void device_register(struct char_device *device) {
    device->next = devices;
    devices = device;
}

int device_open(struct vnode *node, uint32_t flags, struct open_file **result) {
    if (node->type == VFS_BLK) {
        struct block_device *block = block_find(node->rdev);
        if (!block) return -NARC_NO_DEVICE;
        int status = block_open(block, flags, result);
        if (!status) (*result)->node = vnode_get(node);
        return status;
    }
    for (struct char_device *device = devices; device; device = device->next) {
        if (device->rdev != node->rdev) continue;
        int status = device->open(device, flags, result);
        if (status) return status;
        (*result)->node = vnode_get(node);
        (*result)->flags = flags & VFS_STATUS_FLAGS;
        return 0;
    }
    return -NARC_NO_DEVICE;
}

static void node(const char *path, uint32_t mode, uint32_t rdev) {
    vfs_mknod(vfs_root(), path, mode, rdev);
}

void devfs_init(void) {
    vfs_mkdir(vfs_root(), "/dev", 0755);
    if (vfs_mount(vfs_root(), 0, "/dev", "tmpfs", 0) != 0) return;
    node("/dev/null", S_IFCHR | 0666, DEVICE_MAKE(1, 3));
    node("/dev/zero", S_IFCHR | 0666, DEVICE_MAKE(1, 5));
    node("/dev/full", S_IFCHR | 0666, DEVICE_MAKE(1, 7));
    node("/dev/random", S_IFCHR | 0666, DEVICE_MAKE(1, 8));
    node("/dev/urandom", S_IFCHR | 0666, DEVICE_MAKE(1, 9));
    node("/dev/tty", S_IFCHR | 0666, DEVICE_MAKE(5, 0));
    node("/dev/console", S_IFCHR | 0600, DEVICE_MAKE(5, 1));
    block_publish();
}
