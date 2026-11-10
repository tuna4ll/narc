#include <stdbool.h>
#include <stdint.h>

#include "terminal.h"
#include "process.h"
#include "vmm.h"
#include "keyboard.h"

#define SYS_READ  0
#define SYS_WRITE 1
#define SYS_YIELD 24
#define SYS_SLEEP 35
#define SYS_SPAWN 59
#define SYS_EXIT  60
#define SYS_WAIT  61

#define INT80_LENGTH 2

struct syscall_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
};

static bool copy_user_string(uint64_t addr, char *out, int max) {
    for (int i = 0; i < max; i++) {
        if (!vmm_user_ok(addr + i, 1, false))
            return false;
        out[i] = ((const char *)addr)[i];
        if (out[i] == '\0')
            return true;
    }
    return false;
}

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
    if (regs->rax == SYS_READ) {
        char *buf = (char *)regs->rdi;
        uint64_t len = regs->rsi;

        if (len > 4096 || !vmm_user_ok(regs->rdi, len, true)) {
            regs->rax = -1;
            return (uint64_t)regs;
        }

        uint64_t n = 0;
        while (n < len && keyboard_read_char(&buf[n]))
            n++;

        // run the int again once a key arrives
        if (n == 0 && len > 0) {
            process_block_read();
            regs->rip -= INT80_LENGTH;
            return schedule((uint64_t)regs);
        }

        regs->rax = n;
    } else if (regs->rax == SYS_WRITE) {
        const char *buf = (const char *)regs->rdi;
        uint64_t len = regs->rsi;

        if (len > 4096 || !vmm_user_ok(regs->rdi, len, false)) {
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
    } else if (regs->rax == SYS_SLEEP) {
        process_sleep(regs->rdi);
        regs->rax = 0;
        return schedule((uint64_t)regs);
    } else if (regs->rax == SYS_SPAWN) {
        char name[PROC_NAME_LEN];
        regs->rax = copy_user_string(regs->rdi, name, sizeof(name)) ? process_spawn(name) : -1;
    } else if (regs->rax == SYS_WAIT) {
        if (regs->rsi != 0 && !vmm_user_ok(regs->rsi, sizeof(int), true)) {
            regs->rax = -1;
            return (uint64_t)regs;
        }

        int exit_code;
        int result = process_wait((int)regs->rdi, &exit_code);

        if (result == 0) {
            regs->rip -= INT80_LENGTH;
            return schedule((uint64_t)regs);
        }

        if (result > 0 && regs->rsi != 0)
            *(int *)regs->rsi = exit_code;
        regs->rax = result > 0 ? regs->rdi : (uint64_t)-1;
    } else {
        regs->rax = -1;
    }
    return (uint64_t)regs;
}
