#pragma once
#include <kernel/arch.h>
#include <kernel/mm.h>
#include <stdint.h>

enum task_state {
    TASK_NEW,
    TASK_RUNNABLE,
    TASK_SLEEPING,
    TASK_ZOMBIE,
};

struct fd_table;

struct task {
    struct task *next;
    struct task *run_next;
    int pid, ppid;
    int state;
    int on_cpu, on_rq;
    int exited, exit_status;
    uint64_t context;
    uint64_t kstack_top;
    struct task_frame *frame;
    struct address_space space;
    uint64_t fs_base, mmap_next;
    struct fd_table *files;
    uint8_t arch_state[ARCH_STATE_SIZE] __attribute__((aligned(16)));
};

struct task *task_current(void);
struct task *task_create(void);
void task_set_entry(struct task *task, uint64_t pc, uint64_t sp);
int task_fork(struct task_frame *frame);
long task_wait(int pid, uint64_t status, int options);
void task_exec(struct task_frame *frame, struct address_space *space, uint64_t pc, uint64_t sp);
void task_exit(int status) __attribute__((noreturn));
int task_pid(void);
uint64_t task_fs_base(void);
void task_set_fs_base(uint64_t value);
uint64_t task_mmap_next(void);
void task_set_mmap_next(uint64_t value);
struct fd_table *task_files(void);
