#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/file.h>
#include <kernel/heap.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/tty.h>
#include <kernel/uaccess.h>

enum { TASK_UNUSED, TASK_RUNNABLE, TASK_BLOCKED, TASK_ZOMBIE };

struct task {
    struct task *next;
    int pid, ppid, state, exit_status, wait_pid;
    uint64_t wait_status;
    struct address_space space;
    struct task_frame frame;
    uint64_t fs_base, mmap_next;
    uint8_t arch_state[ARCH_STATE_SIZE] __attribute__((aligned(16)));
    struct fd_table *files;
};

static struct task *task_list;
static int next_pid = 1;
static struct task *current;

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

static int init_fds(struct task *task) {
    task->files = fd_table_create();
    if (!task->files) return -1;
    for (int fd = 0; fd < 3; fd++) {
        struct open_file *open = tty_open();
        if (!open || fd_install(task->files, open, fd) != fd) {
            file_put(open);
            fd_table_release(task->files);
            task->files = 0;
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
    child->files = fd_table_clone(current->files);
    if (!child->files) {
        vmm_space_destroy(&child->space);
        task_free(child);
        return -1;
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

static int deliver(struct task *parent, struct task *child) {
    if (!parent || parent->state != TASK_BLOCKED ||
        (parent->wait_pid != -1 && parent->wait_pid != child->pid))
        return 0;
    copy_status(parent, parent->wait_status, child->exit_status << 8);
    arch_syscall_return2(&parent->frame, (uint64_t)child->pid, 0);
    parent->state = TASK_RUNNABLE;
    return 1;
}

static void reparent(struct task *old) {
    struct task *init = find_task(1);
    if (init == old) init = 0;
    for (struct task *child = task_list, *next; child; child = next) {
        next = child->next;
        if (child->ppid != old->pid) continue;
        child->ppid = init ? init->pid : 0;
        if (child->state == TASK_ZOMBIE && (!init || deliver(init, child))) task_free(child);
    }
}

void task_exit(struct task_frame *frame, int status) {
    struct task *old = current;
    fd_table_release(old->files);
    old->files = 0;
    old->state = TASK_ZOMBIE;
    old->exit_status = status & 0xff;
    reparent(old);
    struct task *parent = find_task(old->ppid);
    int reap = !parent || deliver(parent, old);
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

struct fd_table *task_files(void) { return current->files; }
