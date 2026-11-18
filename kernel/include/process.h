#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#include "vfs.h"

#define MAX_PROCESSES 16

#define USER_CODE_ADDR   0x400000
#define USER_STACK_TOP   0x800000
#define USER_STACK_PAGES 4
#define USER_MMAP_BASE   0x100000000000ull
#define USER_MMAP_END    0x7F0000000000ull
#define KERNEL_STACK_SIZE 16384
#define QUANTUM_TICKS 5

#define PROC_UNUSED  0
#define PROC_READY   1
#define PROC_RUNNING 2
#define PROC_DEAD    3
#define PROC_SLEEPING 4
#define PROC_WAITING 5
#define PROC_ZOMBIE  6
#define PROC_READING 7

#define PROC_NAME_LEN 32
#define ARGS_MAX 256
#define MAX_FILES 8

// node is null when the slot is free
struct file {
    struct vnode *node;
    uint64_t offset;
};

struct process {
    int pid;
    int state;
    char name[PROC_NAME_LEN];
    uint64_t pml4;
    uint8_t *kernel_stack;
    uint64_t rsp;
    struct process *parent;
    int exit_code;
    int wait_pid;
    uint64_t wake_tick;
    struct file files[MAX_FILES];
    char cwd[VFS_PATH_MAX];
    uint64_t mmap_next;
};

struct process *process_create(const char *args, uint64_t args_len, const uint8_t *image, uint64_t size);
int process_spawn(char *line);
int process_wait(int pid, int *exit_code);
void process_sleep(uint64_t ms);
void process_block_read(void);
void process_wake_readers(void);
void process_exit(int code);
void process_interrupt(void);

int process_open(const char *path);
struct file *process_file(int fd);
int process_close(int fd);
int process_chdir(const char *path);
const char *process_cwd(void);
int64_t process_mmap(uint64_t len, int prot);
int64_t process_munmap(uint64_t addr, uint64_t len);

__attribute__((noreturn))
void scheduler_start(void);

uint64_t schedule(uint64_t rsp);
uint64_t scheduler_tick(uint64_t rsp);
uint64_t schedule_if_needed(uint64_t rsp);

#endif
