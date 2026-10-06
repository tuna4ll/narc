#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/uaccess.h>
#include <kernel/vfs.h>

#define PIPE_SIZE PAGE_SIZE

enum { TASK_UNUSED, TASK_RUNNABLE, TASK_BLOCKED, TASK_ZOMBIE };
enum { FD_CONSOLE, FD_VFS, FD_PIPE_R, FD_PIPE_W };

struct pipe {
    int refs;
    size_t head, count;
    uint64_t page;
    uint8_t *data;
};

struct open_file {
    int refs, type;
    struct pipe *pipe;
    struct file file;
};

struct task {
    struct task *next;
    int pid, ppid, state, exit_status, wait_pid;
    uint64_t wait_status;
    struct address_space space;
    struct task_frame frame;
    uint64_t fs_base, mmap_next;
    uint8_t arch_state[ARCH_STATE_SIZE] __attribute__((aligned(16)));
    struct open_file **fds;
    int fd_count;
};

static struct task *task_list;
static int next_pid = 1;
static struct task *current;
static char input[256];
static size_t input_pos, input_count;

extern void task_enter(struct task_frame *frame) __attribute__((noreturn));

static struct task *after(struct task *task) {
    return task && task->next ? task->next : task_list;
}

static struct task *next_task(void) {
    struct task *start = after(current), *task = start;
    while (task) {
        if (task->state == TASK_RUNNABLE) return task;
        task = after(task);
        if (task == start) break;
    }
    return 0;
}

static struct task *find_task(int pid) {
    for (struct task *task = task_list; task; task = task->next)
        if (task->pid == pid) return task;
    return 0;
}

static int alloc_pid(void) {
    for (;;) {
        int pid = next_pid;
        next_pid = next_pid == INT32_MAX ? 1 : next_pid + 1;
        if (!find_task(pid)) return pid;
    }
}

static struct task *task_alloc(void) {
    struct task *task = kzalloc(sizeof(*task));
    if (!task) return 0;
    struct task **link = &task_list;
    while (*link) link = &(*link)->next;
    *link = task;
    task->pid = alloc_pid();
    return task;
}

static void task_free(struct task *task) {
    struct task **link = &task_list;
    while (*link && *link != task) link = &(*link)->next;
    if (*link) *link = task->next;
    kfree(task);
}

static void switch_to(struct task *next, struct task_frame *frame) {
    if (current) {
        current->frame = *frame;
        arch_task_state_save(current->arch_state);
    }
    current = next;
    vmm_space_activate(&current->space);
    arch_set_tls(current->fs_base);
    arch_task_state_restore(current->arch_state);
    *frame = current->frame;
}

static struct open_file *file_new(int type) {
    struct open_file *open = kzalloc(sizeof(*open));
    if (!open) return 0;
    open->refs = 1;
    open->type = type;
    return open;
}

static void pipe_put(struct pipe *pipe) {
    if (--pipe->refs) return;
    pmm_free_page(pipe->page);
    kfree(pipe);
}

static void file_put(struct open_file *open) {
    if (!open || --open->refs) return;
    if (open->type == FD_VFS) vfs_close(&open->file);
    if (open->pipe) pipe_put(open->pipe);
    kfree(open);
}

static int fd_reserve(struct task *task, int fd) {
    if (fd < task->fd_count) return 0;
    if (fd == INT32_MAX) return -1;
    int count = task->fd_count ? task->fd_count : 8;
    while (count <= fd) count = count > INT32_MAX / 2 ? INT32_MAX : count * 2;
    struct open_file **fds = krealloc(task->fds, (size_t)count * sizeof(*fds));
    if (!fds) return -1;
    memset(fds + task->fd_count, 0, (size_t)(count - task->fd_count) * sizeof(*fds));
    task->fds = fds;
    task->fd_count = count;
    return 0;
}

static int fd_install(struct task *task, struct open_file *open, int minimum) {
    int fd = minimum;
    while (fd < task->fd_count && task->fds[fd]) fd++;
    if (fd_reserve(task, fd) != 0) return -1;
    task->fds[fd] = open;
    return fd;
}

static void close_all(struct task *task) {
    for (int fd = 0; fd < task->fd_count; fd++) file_put(task->fds[fd]);
    kfree(task->fds);
    task->fds = 0;
    task->fd_count = 0;
}

static int init_fds(struct task *task) {
    for (int fd = 0; fd < 3; fd++) {
        struct open_file *open = file_new(FD_CONSOLE);
        if (!open || fd_install(task, open, fd) != fd) {
            file_put(open);
            close_all(task);
            return -1;
        }
    }
    return 0;
}

struct task *task_create(void) {
    struct task *task = task_alloc();
    if (!task) return 0;
    if (vmm_space_create(&task->space) != 0) {
        task_free(task);
        return 0;
    }
    task->state = TASK_RUNNABLE;
    task->mmap_next = USER_MMAP_BASE;
    arch_task_frame_init(&task->frame);
    arch_task_state_init(task->arch_state);
    if (init_fds(task) != 0) {
        vmm_space_destroy(&task->space);
        task_free(task);
        return 0;
    }
    return task;
}

struct address_space *task_space(struct task *task) {
    return &task->space;
}

void task_set_entry(struct task *task, uint64_t pc, uint64_t sp) {
    arch_task_set_entry(&task->frame, pc, sp);
}

void task_start(void) {
    current = next_task();
    if (!current) goto done;
    vmm_space_activate(&current->space);
    arch_set_tls(current->fs_base);
    arch_task_state_restore(current->arch_state);
    task_enter(&current->frame);
done:
    arch_halt();
}

void task_yield(struct task_frame *frame) {
    struct task *next = next_task();
    if (next && next != current) switch_to(next, frame);
}

void task_preempt(struct task_frame *frame) {
    task_yield(frame);
}

int task_fork(struct task_frame *frame) {
    struct task *child = task_alloc();
    if (!child) return -1;
    if (vmm_space_clone(&child->space, &current->space) != 0) {
        task_free(child);
        return -1;
    }
    child->fds = kmalloc((size_t)current->fd_count * sizeof(*child->fds));
    if (!child->fds) {
        vmm_space_destroy(&child->space);
        task_free(child);
        return -1;
    }
    child->fd_count = current->fd_count;
    for (int fd = 0; fd < child->fd_count; fd++) {
        child->fds[fd] = current->fds[fd];
        if (child->fds[fd]) child->fds[fd]->refs++;
    }
    child->ppid = current->pid;
    child->state = TASK_RUNNABLE;
    child->mmap_next = current->mmap_next;
    child->fs_base = current->fs_base;
    child->frame = *frame;
    arch_syscall_return2(&child->frame, 0, 0);
    arch_task_state_save(current->arch_state);
    memcpy(child->arch_state, current->arch_state, ARCH_STATE_SIZE);
    return child->pid;
}

static int copy_status(struct task *task, uint64_t dst, int status) {
    if (!dst) return 0;
    if (!vmm_user_range_ok(&task->space, dst, sizeof(status), 1)) return -1;
    return uaccess_write(&task->space, dst, &status, sizeof(status));
}

static int matches(struct task *child, struct task *parent, int pid) {
    return child->ppid == parent->pid && (pid == -1 || pid == child->pid);
}

int task_wait(struct task_frame *frame, int pid, uint64_t status, int options, long *result) {
    int found = 0;
    if (status && !vmm_user_range_ok(&current->space, status, sizeof(int), 1)) {
        *result = -1;
        return 0;
    }
    for (struct task *child = task_list; child; child = child->next) {
        if (!matches(child, current, pid)) continue;
        found = 1;
        if (child->state != TASK_ZOMBIE) continue;
        copy_status(current, status, child->exit_status << 8);
        *result = child->pid;
        task_free(child);
        return 0;
    }
    if (!found) {
        *result = -1;
        return 0;
    }
    if (options & 1) {
        *result = 0;
        return 0;
    }
    current->state = TASK_BLOCKED;
    current->wait_pid = pid;
    current->wait_status = status;
    struct task *next = next_task();
    if (!next) {
        current->state = TASK_RUNNABLE;
        *result = -1;
        return 0;
    }
    switch_to(next, frame);
    return 1;
}

void task_exec(struct task_frame *frame, struct address_space *space, uint64_t pc, uint64_t sp) {
    struct address_space old = current->space;
    current->space = *space;
    current->fs_base = 0;
    current->mmap_next = USER_MMAP_BASE;
    arch_task_frame_init(frame);
    arch_task_set_entry(frame, pc, sp);
    arch_task_state_init(current->arch_state);
    arch_task_state_restore(current->arch_state);
    vmm_space_activate(&current->space);
    arch_set_tls(0);
    vmm_space_destroy(&old);
}

void task_exit(struct task_frame *frame, int status) {
    struct task *old = current;
    close_all(old);
    old->state = TASK_ZOMBIE;
    old->exit_status = status & 0xff;
    struct task *parent = find_task(old->ppid);
    int reap = 0;
    if (parent && parent->state == TASK_BLOCKED &&
        (parent->wait_pid == -1 || parent->wait_pid == old->pid)) {
        copy_status(parent, parent->wait_status, old->exit_status << 8);
        arch_syscall_return2(&parent->frame, (uint64_t)old->pid, 0);
        parent->state = TASK_RUNNABLE;
        reap = 1;
    }
    struct task *next = next_task();
    if (!next) {
        console_puts("[kernel] userspace exited\n");
        arch_halt();
    }
    switch_to(next, frame);
    vmm_space_destroy(&old->space);
    if (reap) task_free(old);
}

int task_pid(void) { return current->pid; }
uint64_t task_fs_base(void) { return current->fs_base; }
uint64_t task_mmap_next(void) { return current->mmap_next; }

void task_set_fs_base(uint64_t value) {
    current->fs_base = value;
    arch_set_tls(value);
}

void task_set_mmap_next(uint64_t value) { current->mmap_next = value; }

int task_fd_open(const char *path, uint32_t flags, int *status) {
    struct open_file *open = file_new(FD_VFS);
    if (!open) return -2;
    enum vfs_status result = vfs_open(path, flags, &open->file);
    if (result != VFS_OK) {
        kfree(open);
        *status = result;
        return -1;
    }
    int fd = fd_install(current, open, 3);
    if (fd < 0) file_put(open);
    return fd < 0 ? -2 : fd;
}

static struct open_file *fd_get(int fd) {
    if (fd < 0 || fd >= current->fd_count) return 0;
    return current->fds[fd];
}

struct file *task_fd_file(int fd) {
    struct open_file *open = fd_get(fd);
    return open && open->type == FD_VFS ? &open->file : 0;
}

long task_fd_read(int fd, void *buf, size_t len) {
    struct open_file *open = fd_get(fd);
    if (!open) return -1;
    if (open->type == FD_VFS) return vfs_read(&open->file, buf, len);
    if (open->type == FD_CONSOLE) {
        if (input_pos == input_count) {
            input_pos = input_count = 0;
            while (input_count < sizeof(input)) {
                char c = serial_getc();
                if (c == '\r') c = '\n';
                if ((c == '\b' || c == 127) && input_count) {
                    input_count--;
                    console_write("\b \b", 3);
                    continue;
                }
                if (c == '\b' || c == 127) continue;
                input[input_count++] = c;
                console_write(&c, 1);
                if (c == '\n') break;
            }
        }
        size_t done = len < input_count - input_pos ? len : input_count - input_pos;
        memcpy(buf, input + input_pos, done);
        input_pos += done;
        if (input_pos == input_count) input_pos = input_count = 0;
        return (long)done;
    }
    if (open->type != FD_PIPE_R) return -1;
    struct pipe *pipe = open->pipe;
    size_t done = len < pipe->count ? len : pipe->count;
    for (size_t i = 0; i < done; i++) {
        ((uint8_t *)buf)[i] = pipe->data[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE_SIZE;
    }
    pipe->count -= done;
    return (long)done;
}

long task_fd_write(int fd, const void *buf, size_t len) {
    struct open_file *open = fd_get(fd);
    if (!open) return -1;
    if (open->type == FD_CONSOLE) {
        console_write(buf, len);
        return (long)len;
    }
    if (open->type == FD_VFS) return vfs_write(&open->file, buf, len);
    if (open->type != FD_PIPE_W) return -1;
    struct pipe *pipe = open->pipe;
    size_t done = len < PIPE_SIZE - pipe->count ? len : PIPE_SIZE - pipe->count;
    for (size_t i = 0; i < done; i++)
        pipe->data[(pipe->head + pipe->count + i) % PIPE_SIZE] = ((const uint8_t *)buf)[i];
    pipe->count += done;
    return (long)done;
}

int task_fd_close(int fd) {
    struct open_file *open = fd_get(fd);
    if (!open) return -1;
    current->fds[fd] = 0;
    file_put(open);
    return 0;
}

int task_fd_valid(int fd) { return fd_get(fd) != 0; }

int task_fd_dup(int oldfd, int minimum) {
    struct open_file *open = fd_get(oldfd);
    if (!open || minimum < 0) return -1;
    int fd = fd_install(current, open, minimum);
    if (fd >= 0) open->refs++;
    return fd;
}

int task_fd_dup2(int oldfd, int newfd) {
    struct open_file *open = fd_get(oldfd);
    if (!open || newfd < 0) return -1;
    if (oldfd == newfd) return newfd;
    if (fd_reserve(current, newfd) != 0) return -1;
    open->refs++;
    file_put(current->fds[newfd]);
    current->fds[newfd] = open;
    return newfd;
}

int task_fd_pipe(int fds[2]) {
    struct pipe *pipe = kzalloc(sizeof(*pipe));
    struct open_file *r = file_new(FD_PIPE_R), *w = file_new(FD_PIPE_W);
    if (pipe) pipe->page = pmm_alloc_page();
    if (!pipe || !pipe->page || !r || !w) {
        if (pipe && pipe->page) pmm_free_page(pipe->page);
        kfree(pipe);
        kfree(r);
        kfree(w);
        return -1;
    }
    pipe->data = phys_to_virt(pipe->page);
    pipe->refs = 2;
    r->pipe = w->pipe = pipe;
    fds[0] = fd_install(current, r, 0);
    fds[1] = fds[0] < 0 ? -1 : fd_install(current, w, 0);
    if (fds[1] < 0) {
        if (fds[0] >= 0) current->fds[fds[0]] = 0;
        file_put(r);
        file_put(w);
        return -1;
    }
    return 0;
}
