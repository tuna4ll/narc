#include <kernel/heap.h>
#include <kernel/keyboard.h>
#include <kernel/mm.h>
#include <kernel/spinlock.h>
#include <kernel/virtio.h>

#define EV_KEY 1

struct input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

struct virtio_input {
    struct virtio_device *device;
    struct virtqueue events;
    struct input_event *buffers;
    struct spinlock lock;
};

static void offer(struct virtqueue *queue, uint16_t id) {
    uint16_t slot = queue->avail->index;
    queue->avail->ring[slot % queue->size] = id;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    queue->avail->index = (uint16_t)(slot + 1);
}

static void poll(void *data) {
    struct virtio_input *input = data;
    struct virtqueue *queue = &input->events;
    if (!spin_trylock(&input->lock)) return;
    int returned = 0;
    while (queue->used->index != queue->last_used) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        uint16_t id = (uint16_t)queue->used->ring[queue->last_used % queue->size].id;
        struct input_event event = input->buffers[id];
        queue->last_used++;
        offer(queue, id);
        returned = 1;
        if (event.type == EV_KEY) keyboard_event(event.code, event.value != 0);
    }
    if (returned) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        input->device->transport->notify(input->device, queue->index);
    }
    spin_unlock(&input->lock);
}

int virtio_input_attach(struct virtio_device *device) {
    struct virtio_input *input = kzalloc(sizeof(*input));
    if (!input) return -1;
    uint64_t page = pmm_alloc_page();
    if (!page || virtio_initialize(device, 0) != 0 ||
        virtio_queue_create(device, &input->events, 0) != 0) {
        kfree(input);
        return -1;
    }
    input->device = device;
    input->buffers = phys_to_virt(page);
    spin_init(&input->lock);
    struct virtqueue *queue = &input->events;
    for (uint16_t i = 0; i < queue->size; i++) {
        queue->desc[i].address = page + i * sizeof(struct input_event);
        queue->desc[i].length = sizeof(struct input_event);
        queue->desc[i].flags = VIRTQ_DESC_WRITE;
        queue->desc[i].next = 0;
        offer(queue, i);
    }
    virtio_ready(device);
    device->transport->notify(device, queue->index);
    keyboard_add_poller(poll, input);
    return 0;
}
