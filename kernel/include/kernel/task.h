#pragma once
#include <kernel/arch.h>
#include <kernel/mm.h>
#include <stdint.h>

struct task;
struct fd_table;

struct task *task_create(void);
struct address_space *task_space(struct task *task);
void task_set_entry(struct task *task, uint64_t rip, uint64_t rsp);
void task_start(void) __attribute__((noreturn));
void task_yield(struct task_frame *frame);
void task_preempt(struct task_frame *frame);
int task_fork(struct task_frame *frame);
int task_wait(struct task_frame *frame, int pid, uint64_t status, int options, long *result);
void task_exec(struct task_frame *frame, struct address_space *space, uint64_t pc, uint64_t sp);
void task_exit(struct task_frame *frame, int status);
int task_pid(void);
uint64_t task_fs_base(void);
void task_set_fs_base(uint64_t value);
uint64_t task_mmap_next(void);
void task_set_mmap_next(uint64_t value);
struct fd_table *task_files(void);
