#pragma once
#include <kernel/arch.h>
#include <kernel/mm.h>
#include <stdint.h>

struct task;
struct file;

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
int task_fd_open(const char *path, uint32_t flags, int *status);
struct file *task_fd_file(int fd);
long task_fd_read(int fd, void *buf, size_t len);
long task_fd_write(int fd, const void *buf, size_t len);
int task_fd_close(int fd);
int task_fd_valid(int fd);
int task_fd_dup(int oldfd, int minimum);
int task_fd_dup2(int oldfd, int newfd);
int task_fd_pipe(int fds[2]);
