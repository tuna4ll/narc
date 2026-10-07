#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/pipe.h>

#define PIPE_SIZE PAGE_SIZE

struct pipe {
    int refs;
    size_t head, count;
    uint64_t page;
    uint8_t *data;
};

static void pipe_put(struct pipe *pipe) {
    if (--pipe->refs) return;
    pmm_free_page(pipe->page);
    kfree(pipe);
}

static long pipe_read(struct open_file *open, void *buffer, size_t length) {
    struct pipe *pipe = open->private;
    size_t done = length < pipe->count ? length : pipe->count;
    for (size_t i = 0; i < done; i++) {
        ((uint8_t *)buffer)[i] = pipe->data[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE_SIZE;
    }
    pipe->count -= done;
    return (long)done;
}

static long pipe_write(struct open_file *open, const void *buffer, size_t length) {
    struct pipe *pipe = open->private;
    size_t done = length < PIPE_SIZE - pipe->count ? length : PIPE_SIZE - pipe->count;
    for (size_t i = 0; i < done; i++)
        pipe->data[(pipe->head + pipe->count + i) % PIPE_SIZE] = ((const uint8_t *)buffer)[i];
    pipe->count += done;
    return (long)done;
}

static void pipe_release(struct open_file *open) {
    pipe_put(open->private);
}

static const struct file_ops pipe_read_ops = { .read = pipe_read, .release = pipe_release };
static const struct file_ops pipe_write_ops = { .write = pipe_write, .release = pipe_release };

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
    pipe->data = phys_to_virt(pipe->page);
    pipe->refs = 2;
    return 0;
}
