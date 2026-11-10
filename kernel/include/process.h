#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define MAX_PROCESSES 16

#define USER_CODE_ADDR   0x400000
#define USER_STACK_TOP   0x800000
#define USER_STACK_PAGES 4
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
};

struct process *process_create(const char *name, const uint8_t *image, uint64_t size);
int process_spawn(const char *name);
int process_wait(int pid, int *exit_code);
void process_sleep(uint64_t ms);
void process_block_read(void);
void process_wake_readers(void);

__attribute__((noreturn))
void scheduler_start(void);

uint64_t schedule(uint64_t rsp);
uint64_t scheduler_tick(uint64_t rsp);

__attribute__((noreturn))
void process_exit(int64_t code);

#endif
