#include <stdint.h>

#include "terminal.h"
#include "process.h"

#define SYS_WRITE 1
#define SYS_YIELD 24
#define SYS_EXIT  60

struct syscall_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
};

__asm__(
    ".global syscall_entry\n"
    "syscall_entry:\n"
    "    push %rax\n"
    "    push %rbx\n"
    "    push %rcx\n"
    "    push %rdx\n"
    "    push %rsi\n"
    "    push %rdi\n"
    "    push %rbp\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %r10\n"
    "    push %r11\n"
    "    push %r12\n"
    "    push %r13\n"
    "    push %r14\n"
    "    push %r15\n"
    "    mov %rsp, %rdi\n"
    "    call syscall_handler\n"
    "    mov %rax, %rsp\n"
    "    pop %r15\n"
    "    pop %r14\n"
    "    pop %r13\n"
    "    pop %r12\n"
    "    pop %r11\n"
    "    pop %r10\n"
    "    pop %r9\n"
    "    pop %r8\n"
    "    pop %rbp\n"
    "    pop %rdi\n"
    "    pop %rsi\n"
    "    pop %rdx\n"
    "    pop %rcx\n"
    "    pop %rbx\n"
    "    pop %rax\n"
    "    iretq\n"
);

__attribute__((used))
uint64_t syscall_handler(struct syscall_regs *regs) {
    if (regs->rax == SYS_WRITE) {
        const char *buf = (const char *)regs->rdi;
        uint64_t len = regs->rsi;

        if (regs->rdi >= 0x800000000000 || len > 4096) {
            regs->rax = -1;
            return (uint64_t)regs;
        }

        for (uint64_t i = 0; i < len; i++)
            terminal_putchar(buf[i]);

        regs->rax = len;
    } else if (regs->rax == SYS_YIELD) {
        regs->rax = 0;
        return schedule((uint64_t)regs);
    } else if (regs->rax == SYS_EXIT) {
        process_exit((int)regs->rdi);
    } else {
        regs->rax = -1;
    }
    return (uint64_t)regs;
}
