#include <kernel/device.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <narc/abi.h>

static uint64_t random_state;

static long null_read(struct open_file *open, void *buffer, size_t length) {
    (void)open;
    (void)buffer;
    (void)length;
    return 0;
}

static long sink_write(struct open_file *open, const void *buffer, size_t length) {
    (void)open;
    (void)buffer;
    return (long)length;
}

static long zero_read(struct open_file *open, void *buffer, size_t length) {
    (void)open;
    memset(buffer, 0, length);
    return (long)length;
}

static long full_write(struct open_file *open, const void *buffer, size_t length) {
    (void)open;
    (void)buffer;
    (void)length;
    return -NARC_NO_SPACE;
}

static long random_read(struct open_file *open, void *buffer, size_t length) {
    (void)open;
    uint8_t *out = buffer;
    if (!random_state) random_state = time_monotonic_ns() ^ 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < length; i++) {
        random_state ^= random_state << 13;
        random_state ^= random_state >> 7;
        random_state ^= random_state << 17;
        out[i] = (uint8_t)(random_state >> 24);
    }
    return (long)length;
}

static const struct file_ops null_ops = { .read = null_read, .write = sink_write };
static const struct file_ops zero_ops = { .read = zero_read, .write = sink_write };
static const struct file_ops full_ops = { .read = zero_read, .write = full_write };
static const struct file_ops random_ops = { .read = random_read, .write = sink_write };

static int open_memory(struct char_device *device, uint32_t flags, struct open_file **result) {
    (void)flags;
    *result = file_alloc(device->data, 0);
    return *result ? 0 : -NARC_NO_MEMORY;
}

static struct char_device memory_devices[] = {
    { 0, DEVICE_MAKE(1, 3), "null", open_memory, (void *)&null_ops },
    { 0, DEVICE_MAKE(1, 5), "zero", open_memory, (void *)&zero_ops },
    { 0, DEVICE_MAKE(1, 7), "full", open_memory, (void *)&full_ops },
    { 0, DEVICE_MAKE(1, 8), "random", open_memory, (void *)&random_ops },
    { 0, DEVICE_MAKE(1, 9), "urandom", open_memory, (void *)&random_ops },
};

void memory_devices_init(void) {
    for (size_t i = 0; i < sizeof(memory_devices) / sizeof(memory_devices[0]); i++)
        device_register(&memory_devices[i]);
}
