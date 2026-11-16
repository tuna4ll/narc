#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <stdbool.h>
#include <stdint.h>

#include "vfs.h"

#define INITRAMFS_NAME_LEN 100

void initramfs_init(const uint8_t *base, uint64_t size);
struct vnode *initramfs_root(void);

#endif
