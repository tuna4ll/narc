#pragma once
#include <kernel/file.h>
#include <kernel/mutex.h>
#include <stdint.h>

#define BLOCK_MAJOR 254
#define SECTOR_SIZE 512

struct block_device {
    struct block_device *next;
    char name[16];
    uint32_t rdev;
    uint64_t sectors;
    int (*read)(struct block_device *device, uint64_t sector, uint32_t count, void *buffer);
    int (*write)(struct block_device *device, uint64_t sector, uint32_t count, const void *buffer);
    void *data;
    struct mutex lock;
};

struct buffer {
    struct buffer *next, *previous;
    struct block_device *device;
    uint64_t block;
    uint32_t size;
    uint32_t refs;
    int valid;
    uint8_t *data;
};

void block_register(struct block_device *device);
struct block_device *block_find(uint32_t rdev);
int block_open(struct block_device *device, uint32_t flags, struct open_file **result);
void block_publish(void);
int block_io(struct block_device *device, uint64_t sector, uint32_t count, void *buffer, int write);

struct buffer *bread(struct block_device *device, uint64_t block, uint32_t size);
int bwrite(struct buffer *buffer);
void brelse(struct buffer *buffer);
void binvalidate(struct block_device *device);
