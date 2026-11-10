#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <stdbool.h>
#include <stdint.h>

void initramfs_init(const uint8_t *base, uint64_t size);
bool initramfs_find(const char *name, const uint8_t **data, uint64_t *size);

#endif
