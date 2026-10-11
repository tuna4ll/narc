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
#include <kernel/vfs.h>

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
    wait_queue_init(&task->child_exit);
    return task;
}

static void task_destroy(struct task *task) {
    fd_table_release(task->files);
    vnode_put(task->cwd);
    if (task->space.root) vmm_space_destroy(&task->space);
    kstack_free(task->kstack_top, KSTACK_SIZE);
    kfree(task);
}

static void task_publish(struct task *task) {
    unsigned long flags = spin_lock_irqsave(&task_lock);
    task->pid = alloc_pid();
    struct task **link = &task_list;
    while (*link) link = &(*link)->next;
    *link = task;
    spin_unlock_irqrestore(&task_lock, flags);
}

static void task_unlink(struct task *task) {
    struct task **link = &task_list;
    while (*link && *link != task) link = &(*link)->next;
    if (*link) *link = task->next;
}

static void reap_detached(void) {
    for (;;) {
        struct task *zombie = 0;
        unsigned long flags = spin_lock_irqsave(&task_lock);
        for (struct task *task = task_list; task; task = task->next) {
            if (!task->exited || !task->detached ||
                __atomic_load_n(&task->on_cpu, __ATOMIC_ACQUIRE))
                continue;
            zombie = task;
            task_unlink(task);
            break;
        }
        spin_unlock_irqrestore(&task_lock, flags);
        if (!zombie) return;
        task_destroy(zombie);
    }
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
    task->cwd = vnode_get(vfs_root());
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
    reap_detached();
    struct task *parent = task_current();
    struct task *child = task_alloc();
    if (!child) return -1;
    if (vmm_space_clone(&child->space, &parent->space) != 0 ||
        !(child->files = fd_table_clone(parent->files))) {
        task_destroy(child);
        return -1;
    }
    child->ppid = parent->pid;
    child->cwd = vnode_get(parent->cwd);
    child->mmap_next = parent->mmap_next;
    child->fs_base = parent->fs_base;
    signal_fork(child, parent);
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
    return child->ppid == parent->pid && !child->detached && (pid == -1 || pid == child->pid);
}

long task_wait(int pid, uint64_t status, int options) {
    struct task *self = task_current();
    if (status && !vmm_user_range_ok(&self->space, status, sizeof(int), 1))
        return -NARC_BAD_ADDRESS;
    reap_detached();
    struct task *zombie = 0;
    struct waiter waiter = { 0 };
    long result = 0;
    for (;;) {
        wait_prepare_interruptible(&self->child_exit, &waiter);
        int found = 0;
        unsigned long flags = spin_lock_irqsave(&task_lock);
        for (struct task *child = task_list; child; child = child->next) {
            if (!matches(child, self, pid)) continue;
            found = 1;
            if (!child->exited) continue;
            zombie = child;
            task_unlink(child);
            break;
        }
        spin_unlock_irqrestore(&task_lock, flags);
        if (zombie) break;
        if (!found) {
            result = -NARC_NO_CHILD;
            break;
        }
        if (options & WAIT_NOHANG) break;
        if (signal_pending()) {
            result = -NARC_INTERRUPTED;
            break;
        }
        schedule();
    }
    wait_finish(&self->child_exit, &waiter);
    if (!zombie) return result;
    while (__atomic_load_n(&zombie->on_cpu, __ATOMIC_ACQUIRE)) arch_cpu_relax();
    copy_status(self, status, zombie->wait_status);
    result = zombie->pid;
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
    signal_exec(self);
}

__attribute__((noreturn))
static void do_exit(int wait_status) {
    struct task *self = task_current();
    if (self->pid == 1) {
        console_puts(wait_status & 0x7f ? "[kernel] init killed\n" : "[kernel] init exited\n");
        for (;;) arch_halt();
    }
    fd_table_release(self->files);
    self->files = 0;
    vnode_put(self->cwd);
    self->cwd = 0;
    vmm_space_activate(sched_kernel_space());
    vmm_space_destroy(&self->space);

    unsigned long flags = spin_lock_irqsave(&task_lock);
    struct task *init = find_task(1);
    int orphaned_zombie = 0;
    for (struct task *child = task_list; child; child = child->next) {
        if (child->ppid != self->pid) continue;
        child->ppid = init ? init->pid : 0;
        orphaned_zombie |= child->exited && !child->detached;
    }
    struct task *parent = find_task(self->ppid);
    if (parent) {
        narc_sigaction_t *action = &parent->signals.actions[NARC_SIGCHLD];
        self->detached = action->handler == NARC_SIG_IGN || (action->flags & NARC_SA_NOCLDWAIT);
        int signal = wait_status & 0x7f;
        narc_siginfo_t info = {
            .code = !signal ? NARC_CLD_EXITED : (wait_status & 0x80) ? NARC_CLD_DUMPED :
                    NARC_CLD_KILLED,
            .pid = self->pid,
            .status = signal ? signal : (wait_status >> 8) & 0xff,
        };
        signal_send(parent, NARC_SIGCHLD, &info);
    }
    self->wait_status = wait_status;
    self->exited = 1;
    if (parent) wake_up_all(&parent->child_exit);
    if (init && orphaned_zombie) wake_up_all(&init->child_exit);
    spin_unlock_irqrestore(&task_lock, flags);
    sched_exit();
}

void task_exit(int status) {
    do_exit((status & 0xff) << 8);
}

void task_exit_signal(int signal) {
    arch_irq_enable();
    do_exit(signal & 0xff);
}

int task_kill(int pid, int signal) {
    if (signal < 0 || signal >= NARC_NSIG) return -NARC_INVALID_ARGUMENT;
    struct task *self = task_current();
    narc_siginfo_t info = { .code = NARC_SI_USER, .pid = self->pid };
    int delivered = 0;
    unsigned long flags = spin_lock_irqsave(&task_lock);
    for (struct task *task = task_list; task; task = task->next) {
        if (pid > 0 ? task->pid != pid : pid == -1 ? task->pid == 1 : 1) continue;
        delivered = 1;
        signal_send(task, signal, &info);
    }
    spin_unlock_irqrestore(&task_lock, flags);
    return delivered ? 0 : -NARC_NO_PROCESS;
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
struct vnode *task_cwd(void) { return task_current()->cwd; }

void task_set_cwd(struct vnode *node) {
    struct task *self = task_current();
    struct vnode *old = self->cwd;
    self->cwd = node;
    vnode_put(old);
}
