#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define MAX_PROCESSES 16

#define USER_CODE_ADDR   0x400000
#define USER_STACK_TOP   0x800000
#define USER_STACK_PAGES 4

#define PROC_UNUSED  0
#define PROC_READY   1
#define PROC_RUNNING 2

struct process {
    int pid;
    int state;
    const char *name;
    uint64_t pml4;
};

struct process *process_create(const char *name, const uint8_t *code, uint64_t size);

__attribute__((noreturn))
void process_start_next(void);

__attribute__((noreturn))
void process_exit(int64_t code);

struct process *process_current(void);

#endif
