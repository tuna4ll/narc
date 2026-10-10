#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/pipe.h>
#include <kernel/spinlock.h>
#include <kernel/wait.h>
#include <narc/abi.h>

#define PIPE_SIZE PAGE_SIZE

struct pipe {
    struct spinlock lock;
    int readers, writers;
    size_t head, count;
    uint64_t page;
    uint8_t *data;
    struct wait_queue readable, writable;
};

static void pipe_release(struct pipe *pipe, int reader) {
    spin_lock(&pipe->lock);
    if (reader) pipe->readers--;
    else pipe->writers--;
    int unused = !pipe->readers && !pipe->writers;
    spin_unlock(&pipe->lock);
    wake_up_all(reader ? &pipe->writable : &pipe->readable);
    if (!unused) return;
    pmm_free_page(pipe->page);
    kfree(pipe);
}

static long pipe_read(struct open_file *open, void *buffer, size_t length) {
    struct pipe *pipe = open->private;
    spin_lock(&pipe->lock);
    wait_event_locked(&pipe->readable, pipe->count || !pipe->writers, &pipe->lock);
    size_t done = length < pipe->count ? length : pipe->count;
    for (size_t i = 0; i < done; i++) {
        ((uint8_t *)buffer)[i] = pipe->data[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE_SIZE;
    }
    pipe->count -= done;
    spin_unlock(&pipe->lock);
    if (done) wake_up_all(&pipe->writable);
    return (long)done;
}

static long pipe_write(struct open_file *open, const void *buffer, size_t length) {
    struct pipe *pipe = open->private;
    size_t done = 0;
    while (done < length) {
        spin_lock(&pipe->lock);
        wait_event_locked(&pipe->writable, pipe->count < PIPE_SIZE || !pipe->readers,
                          &pipe->lock);
        if (!pipe->readers) {
            spin_unlock(&pipe->lock);
            return done ? (long)done : -NARC_BROKEN_PIPE;
        }
        size_t space = PIPE_SIZE - pipe->count;
        size_t chunk = length - done < space ? length - done : space;
        for (size_t i = 0; i < chunk; i++)
            pipe->data[(pipe->head + pipe->count + i) % PIPE_SIZE] =
                ((const uint8_t *)buffer)[done + i];
        pipe->count += chunk;
        done += chunk;
        spin_unlock(&pipe->lock);
        wake_up_all(&pipe->readable);
    }
    return (long)done;
}

static void pipe_release_read(struct open_file *open) {
    pipe_release(open->private, 1);
}

static void pipe_release_write(struct open_file *open) {
    pipe_release(open->private, 0);
}

static const struct file_ops pipe_read_ops = { .read = pipe_read, .release = pipe_release_read };
static const struct file_ops pipe_write_ops = { .write = pipe_write, .release = pipe_release_write };

int pipe_create(struct open_file **read_end, struct open_file **write_end) {
    struct pipe *pipe = kzalloc(sizeof(*pipe));
    if (!pipe) return -1;
    pipe->page = pmm_alloc_page();
    *read_end = pipe->page ? file_alloc(&pipe_read_ops, pipe) : 0;
    *write_end = *read_end ? file_alloc(&pipe_write_ops, pipe) : 0;
    if (!*write_end) {
        kfree(*read_end);
        if (pipe->page) pmm_free_page(pipe->page);
        kfree(pipe);
        return -1;
    }
    spin_init(&pipe->lock);
    wait_queue_init(&pipe->readable);
    wait_queue_init(&pipe->writable);
    pipe->data = phys_to_virt(pipe->page);
    pipe->readers = pipe->writers = 1;
    return 0;
}
