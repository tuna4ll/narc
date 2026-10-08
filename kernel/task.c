#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/file.h>
#include <kernel/heap.h>
#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/tty.h>
#include <kernel/uaccess.h>

#define KSTACK_SIZE (16ULL * 1024ULL)
#define WAIT_NOHANG 1

static struct spinlock task_lock = SPINLOCK_INIT;
static struct task *task_list;
static int next_pid = 1;

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

static void task_trampoline(void) {
    sched_tail();
    arch_enter_user(task_current()->frame);
}

static struct task *task_alloc(void) {
    struct task *task = kzalloc(sizeof(*task));
    if (!task) return 0;
    task->kstack_top = kstack_alloc(KSTACK_SIZE);
    if (!task->kstack_top) {
        kfree(task);
        return 0;
    }
    task->frame = (struct task_frame *)(uintptr_t)(task->kstack_top - sizeof(struct task_frame));
    task->context = arch_context_init((uint64_t)(uintptr_t)task->frame, task_trampoline);
    task->state = TASK_NEW;
    return task;
}

static void task_destroy(struct task *task) {
    fd_table_release(task->files);
    if (task->space.root) vmm_space_destroy(&task->space);
    kstack_free(task->kstack_top, KSTACK_SIZE);
    kfree(task);
}

static void task_publish(struct task *task) {
    spin_lock(&task_lock);
    task->pid = alloc_pid();
    struct task **link = &task_list;
    while (*link) link = &(*link)->next;
    *link = task;
    spin_unlock(&task_lock);
}

static void task_unlink(struct task *task) {
    struct task **link = &task_list;
    while (*link && *link != task) link = &(*link)->next;
    if (*link) *link = task->next;
}

static int init_fds(struct task *task) {
    task->files = fd_table_create();
    if (!task->files) return -1;
    for (int fd = 0; fd < 3; fd++) {
        struct open_file *open = tty_open();
        if (!open || fd_install(task->files, open, fd) != fd) {
            file_put(open);
            return -1;
        }
    }
    return 0;
}

struct task *task_create(void) {
    struct task *task = task_alloc();
    if (!task) return 0;
    if (vmm_space_create(&task->space) != 0 || init_fds(task) != 0) {
        task_destroy(task);
        return 0;
    }
    task->mmap_next = USER_MMAP_BASE;
    arch_task_frame_init(task->frame);
    arch_task_state_init(task->arch_state);
    task_publish(task);
    return task;
}

void task_set_entry(struct task *task, uint64_t pc, uint64_t sp) {
    arch_task_set_entry(task->frame, pc, sp);
}

int task_fork(struct task_frame *frame) {
    struct task *parent = task_current();
    struct task *child = task_alloc();
    if (!child) return -1;
    if (vmm_space_clone(&child->space, &parent->space) != 0 ||
        !(child->files = fd_table_clone(parent->files))) {
        task_destroy(child);
        return -1;
    }
    child->ppid = parent->pid;
    child->mmap_next = parent->mmap_next;
    child->fs_base = parent->fs_base;
    *child->frame = *frame;
    arch_syscall_return2(child->frame, 0, 0);
    arch_task_state_save(parent->arch_state);
    memcpy(child->arch_state, parent->arch_state, ARCH_STATE_SIZE);
    task_publish(child);
    int pid = child->pid;
    sched_start(child);
    return pid;
}

static int copy_status(struct task *task, uint64_t dst, int status) {
    if (!dst) return 0;
    if (!vmm_user_range_ok(&task->space, dst, sizeof(status), 1)) return -1;
    return uaccess_write(&task->space, dst, &status, sizeof(status));
}

static int matches(struct task *child, struct task *parent, int pid) {
    return child->ppid == parent->pid && (pid == -1 || pid == child->pid);
}

long task_wait(int pid, uint64_t status, int options) {
    struct task *self = task_current();
    if (status && !vmm_user_range_ok(&self->space, status, sizeof(int), 1)) return -2;
    struct task *zombie = 0;
    int found;
    for (;;) {
        sched_sleep_prepare();
        found = 0;
        spin_lock(&task_lock);
        for (struct task *child = task_list; child; child = child->next) {
            if (!matches(child, self, pid)) continue;
            found = 1;
            if (!child->exited) continue;
            zombie = child;
            task_unlink(child);
            break;
        }
        spin_unlock(&task_lock);
        if (zombie || !found || (options & WAIT_NOHANG)) break;
        schedule();
    }
    sched_sleep_cancel();
    if (!zombie) return found ? 0 : -1;
    while (__atomic_load_n(&zombie->on_cpu, __ATOMIC_ACQUIRE)) arch_cpu_relax();
    copy_status(self, status, zombie->exit_status << 8);
    long result = zombie->pid;
    task_destroy(zombie);
    return result;
}

void task_exec(struct task_frame *frame, struct address_space *space, uint64_t pc, uint64_t sp) {
    struct task *self = task_current();
    struct address_space old = self->space;
    self->space = *space;
    self->fs_base = 0;
    self->mmap_next = USER_MMAP_BASE;
    arch_task_frame_init(frame);
    arch_task_set_entry(frame, pc, sp);
    arch_task_state_init(self->arch_state);
    arch_task_state_restore(self->arch_state);
    vmm_space_activate(&self->space);
    arch_set_tls(0);
    vmm_space_destroy(&old);
}

void task_exit(int status) {
    struct task *self = task_current();
    if (self->pid == 1) {
        console_puts("[kernel] init exited\n");
        for (;;) arch_halt();
    }
    fd_table_release(self->files);
    self->files = 0;
    vmm_space_activate(sched_kernel_space());
    vmm_space_destroy(&self->space);

    spin_lock(&task_lock);
    struct task *init = find_task(1);
    int orphaned_zombie = 0;
    for (struct task *child = task_list; child; child = child->next) {
        if (child->ppid != self->pid) continue;
        child->ppid = init ? init->pid : 0;
        orphaned_zombie |= child->exited;
    }
    self->exit_status = status & 0xff;
    self->exited = 1;
    struct task *parent = find_task(self->ppid);
    if (parent) sched_wake(parent);
    if (init && orphaned_zombie) sched_wake(init);
    spin_unlock(&task_lock);
    sched_exit();
}

int task_pid(void) { return task_current()->pid; }
uint64_t task_fs_base(void) { return task_current()->fs_base; }
uint64_t task_mmap_next(void) { return task_current()->mmap_next; }

void task_set_fs_base(uint64_t value) {
    task_current()->fs_base = value;
    arch_set_tls(value);
}

void task_set_mmap_next(uint64_t value) { task_current()->mmap_next = value; }

struct fd_table *task_files(void) { return task_current()->files; }
