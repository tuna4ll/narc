#pragma once
#include <kernel/arch.h>
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/wait.h>
#include <stdint.h>

enum task_state {
    TASK_NEW,
    TASK_RUNNABLE,
    TASK_SLEEPING,
    TASK_ZOMBIE,
};

struct fd_table;
struct vnode;

struct task {
    struct task *next;
    struct task *run_next;
    int pid, ppid, pgid, sid;
    int state;
    int on_cpu, on_rq;
    int interruptible;
    int exited, detached, wait_status;
    int stopped, stop_signal, stop_reported, continued;
    uint64_t context;
    uint64_t kstack_top;
    struct task_frame *frame;
    struct address_space space;
    uint64_t fs_base, mmap_next;
    struct fd_table *files;
    struct vnode *cwd;
    uint32_t umask;
    struct wait_queue child_exit;
    struct signal_state signals;
    int syscall_restart;
    uint64_t syscall_saved[4];
    uint8_t arch_state[ARCH_STATE_SIZE] __attribute__((aligned(16)));
};

struct task *task_current(void);
struct task *task_create(void);
void task_set_entry(struct task *task, uint64_t pc, uint64_t sp);
int task_fork(struct task_frame *frame);
long task_wait(int pid, uint64_t status, int options);
void task_exec(struct task_frame *frame, struct address_space *space, uint64_t pc, uint64_t sp);
void task_exit(int status) __attribute__((noreturn));
void task_exit_signal(int signal) __attribute__((noreturn));
int task_kill(int pid, int signal);
int task_signal_group(int pgid, int signal);
int task_group_orphaned(int pgid);
void task_stop(int signal);
int task_ppid(void);
int task_setpgid(int pid, int pgid);
int task_getpgid(int pid);
int task_setsid(void);
int task_getsid(int pid);
int task_pid(void);
uint64_t task_fs_base(void);
void task_set_fs_base(uint64_t value);
uint64_t task_mmap_next(void);
void task_set_mmap_next(uint64_t value);
struct fd_table *task_files(void);
struct vnode *task_cwd(void);
uint32_t task_umask(uint32_t mask, int set);
void task_set_cwd(struct vnode *node);
