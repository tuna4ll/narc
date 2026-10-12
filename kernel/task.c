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

static struct spinlock task_lock = SPINLOCK_INIT;
static struct task *task_list;
static int next_pid = 1;

static struct task *find_task(int pid) {
    for (struct task *task = task_list; task; task = task->next)
        if (task->pid == pid) return task;
    return 0;
}

static int id_in_use(int id) {
    for (struct task *task = task_list; task; task = task->next)
        if (task->pid == id || task->pgid == id || task->sid == id) return 1;
    return 0;
}

static int alloc_pid(void) {
    for (;;) {
        int pid = next_pid;
        next_pid = next_pid == INT32_MAX ? 1 : next_pid + 1;
        if (!id_in_use(pid)) return pid;
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
    if (!task->sid) task->pgid = task->sid = task->pid;
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
        if (!open || fd_install(task->files, open, fd, 0) != fd) {
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
    task->umask = 022;
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
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    child->cwd = vnode_get(parent->cwd);
    child->umask = parent->umask;
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
    if (child->ppid != parent->pid || child->detached) return 0;
    if (pid > 0) return child->pid == pid;
    if (pid == 0) return child->pgid == parent->pgid;
    return pid == -1 || child->pgid == -pid;
}

static struct task *wait_event_of(struct task *self, int pid, int options, int *status,
                                  int *found) {
    for (struct task *child = task_list; child; child = child->next) {
        if (!matches(child, self, pid)) continue;
        *found = 1;
        if (child->exited) {
            *status = child->wait_status;
            task_unlink(child);
            return child;
        }
        if ((options & NARC_WAIT_UNTRACED) && child->stopped && !child->stop_reported) {
            child->stop_reported = 1;
            *status = (child->stop_signal << 8) | 0x7f;
            return child;
        }
        if ((options & NARC_WAIT_CONTINUED) && child->continued) {
            child->continued = 0;
            *status = 0xffff;
            return child;
        }
    }
    return 0;
}

long task_wait(int pid, uint64_t status, int options) {
    struct task *self = task_current();
    if (status && !vmm_user_range_ok(&self->space, status, sizeof(int), 1))
        return -NARC_BAD_ADDRESS;
    reap_detached();
    struct task *child = 0;
    struct waiter waiter = { 0 };
    int wait_status = 0;
    long result = 0;
    for (;;) {
        wait_prepare_interruptible(&self->child_exit, &waiter);
        int found = 0;
        unsigned long flags = spin_lock_irqsave(&task_lock);
        child = wait_event_of(self, pid, options, &wait_status, &found);
        result = child ? child->pid : 0;
        spin_unlock_irqrestore(&task_lock, flags);
        if (child) break;
        if (!found) {
            result = -NARC_NO_CHILD;
            break;
        }
        if (options & NARC_WAIT_NOHANG) break;
        if (signal_pending()) {
            result = -NARC_INTERRUPTED;
            break;
        }
        schedule();
    }
    wait_finish(&self->child_exit, &waiter);
    if (!child) return result;
    copy_status(self, status, wait_status);
    if (child->exited) {
        while (__atomic_load_n(&child->on_cpu, __ATOMIC_ACQUIRE)) arch_cpu_relax();
        task_destroy(child);
    }
    return result;
}

void task_exec(struct task_frame *frame, struct address_space *space, uint64_t pc, uint64_t sp) {
    struct task *self = task_current();
    fd_table_exec(self->files);
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

static void notify_parent(struct task *task, int code, int status) {
    struct task *parent = find_task(task->ppid);
    if (!parent) return;
    narc_sigaction_t *action = &parent->signals.actions[NARC_SIGCHLD];
    if (!(action->flags & NARC_SA_NOCLDSTOP)) {
        narc_siginfo_t info = { .code = code, .pid = task->pid, .status = status };
        signal_send(parent, NARC_SIGCHLD, &info);
    }
    wake_up_all(&parent->child_exit);
}

static void send_locked(struct task *task, int signal, const narc_siginfo_t *info) {
    if ((signal == NARC_SIGCONT || signal == NARC_SIGKILL) && task->stopped) {
        __atomic_store_n(&task->stopped, 0, __ATOMIC_RELEASE);
        task->continued = signal == NARC_SIGCONT;
        if (signal == NARC_SIGCONT) notify_parent(task, NARC_CLD_CONTINUED, NARC_SIGCONT);
        sched_wake(task);
    }
    signal_send(task, signal, info);
}

static int orphaned_locked(int pgid) {
    int members = 0;
    for (struct task *task = task_list; task; task = task->next) {
        if (task->pgid != pgid || task->exited) continue;
        members = 1;
        struct task *parent = find_task(task->ppid);
        if (parent && !parent->exited && parent->pgid != pgid && parent->sid == task->sid)
            return 0;
    }
    return members;
}

static void orphan_check(int pgid, int sid) {
    int stopped = 0;
    for (struct task *task = task_list; task; task = task->next)
        stopped |= task->pgid == pgid && task->sid == sid && task->stopped && !task->exited;
    if (!stopped || !orphaned_locked(pgid)) return;
    for (struct task *task = task_list; task; task = task->next) {
        if (task->pgid != pgid || task->exited) continue;
        send_locked(task, NARC_SIGHUP, 0);
        send_locked(task, NARC_SIGCONT, 0);
    }
}

void task_stop(int signal) {
    struct task *self = task_current();
    unsigned long flags = spin_lock_irqsave(&task_lock);
    self->stop_signal = signal;
    self->stop_reported = 0;
    self->continued = 0;
    __atomic_store_n(&self->stopped, 1, __ATOMIC_RELEASE);
    notify_parent(self, NARC_CLD_STOPPED, signal);
    spin_unlock_irqrestore(&task_lock, flags);
    for (;;) {
        sched_sleep_prepare(0);
        if (!__atomic_load_n(&self->stopped, __ATOMIC_ACQUIRE)) break;
        schedule();
    }
    sched_sleep_cancel();
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
    self->exited = 1;
    for (struct task *child = task_list; child; child = child->next) {
        if (child->ppid != self->pid) continue;
        child->ppid = init ? init->pid : 0;
        orphaned_zombie |= child->exited && !child->detached;
        orphan_check(child->pgid, child->sid);
    }
    orphan_check(self->pgid, self->sid);
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

static int targeted(struct task *task, struct task *self, int pid) {
    if (task->exited) return 0;
    if (pid > 0) return task->pid == pid;
    if (pid == 0) return task->pgid == self->pgid;
    if (pid == -1) return task->pid != 1 && task != self;
    return task->pgid == -pid;
}

int task_kill(int pid, int signal) {
    if (signal < 0 || signal >= NARC_NSIG || pid == INT32_MIN) return -NARC_INVALID_ARGUMENT;
    struct task *self = task_current();
    narc_siginfo_t info = { .code = NARC_SI_USER, .pid = self->pid };
    int delivered = 0;
    unsigned long flags = spin_lock_irqsave(&task_lock);
    for (struct task *task = task_list; task; task = task->next) {
        if (!targeted(task, self, pid)) continue;
        delivered = 1;
        if (signal) send_locked(task, signal, &info);
    }
    spin_unlock_irqrestore(&task_lock, flags);
    return delivered ? 0 : -NARC_NO_PROCESS;
}

int task_signal_group(int pgid, int signal) {
    int delivered = 0;
    unsigned long flags = spin_lock_irqsave(&task_lock);
    for (struct task *task = task_list; task; task = task->next) {
        if (task->pgid != pgid || task->exited) continue;
        delivered = 1;
        send_locked(task, signal, 0);
    }
    spin_unlock_irqrestore(&task_lock, flags);
    return delivered ? 0 : -NARC_NO_PROCESS;
}

int task_group_orphaned(int pgid) {
    unsigned long flags = spin_lock_irqsave(&task_lock);
    int orphaned = orphaned_locked(pgid);
    spin_unlock_irqrestore(&task_lock, flags);
    return orphaned;
}

int task_ppid(void) { return task_current()->ppid; }

int task_setpgid(int pid, int pgid) {
    struct task *self = task_current();
    if (pid < 0 || pgid < 0) return -NARC_INVALID_ARGUMENT;
    if (!pid) pid = self->pid;
    if (!pgid) pgid = pid;
    unsigned long flags = spin_lock_irqsave(&task_lock);
    struct task *target = find_task(pid);
    int status = 0;
    if (!target || target->exited || (target != self && target->ppid != self->pid)) {
        status = -NARC_NO_PROCESS;
    } else if (target->sid != self->sid || target->pid == target->sid) {
        status = -NARC_NOT_PERMITTED;
    } else if (pgid != target->pid) {
        status = -NARC_NOT_PERMITTED;
        for (struct task *task = task_list; task; task = task->next)
            if (task->pgid == pgid && task->sid == self->sid && !task->exited) status = 0;
    }
    if (!status) target->pgid = pgid;
    spin_unlock_irqrestore(&task_lock, flags);
    return status;
}

static int lookup_id(int pid, int session) {
    struct task *self = task_current();
    if (pid < 0) return -NARC_INVALID_ARGUMENT;
    if (!pid) return session ? self->sid : self->pgid;
    unsigned long flags = spin_lock_irqsave(&task_lock);
    struct task *task = find_task(pid);
    int result = !task || task->exited ? -NARC_NO_PROCESS : session ? task->sid : task->pgid;
    spin_unlock_irqrestore(&task_lock, flags);
    return result;
}

int task_getpgid(int pid) { return lookup_id(pid, 0); }
int task_getsid(int pid) { return lookup_id(pid, 1); }

int task_setsid(void) {
    struct task *self = task_current();
    unsigned long flags = spin_lock_irqsave(&task_lock);
    int leader = 0;
    for (struct task *task = task_list; task; task = task->next)
        leader |= task->pgid == self->pid;
    if (!leader) self->pgid = self->sid = self->pid;
    spin_unlock_irqrestore(&task_lock, flags);
    return leader ? -NARC_NOT_PERMITTED : self->sid;
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

uint32_t task_umask(uint32_t mask, int set) {
    struct task *self = task_current();
    uint32_t old = self->umask;
    if (set) self->umask = mask & 0777;
    return old;
}

void task_set_cwd(struct vnode *node) {
    struct task *self = task_current();
    struct vnode *old = self->cwd;
    self->cwd = node;
    vnode_put(old);
}
