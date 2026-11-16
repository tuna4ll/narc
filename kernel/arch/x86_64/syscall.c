#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "process.h"
#include "vmm.h"
#include "idt.h"
#include "vfs.h"
#include "mem.h"

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_YIELD   24
#define SYS_SLEEP   35
#define SYS_SPAWN   59
#define SYS_EXIT    60
#define SYS_WAIT    61
#define SYS_GETCWD  79
#define SYS_CHDIR   80
#define SYS_READDIR 89

#define IO_MAX 4096
#define INT80_LENGTH 2

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

static uint64_t sys_read(struct interrupt_frame *regs) {
    struct file *file = process_file((int)regs->rdi);
    char *buf = (char *)regs->rsi;
    uint64_t len = regs->rdx;

    if (file == NULL || len > IO_MAX || !vmm_user_ok(regs->rsi, len, true)) {
        regs->rax = -1;
        return (uint64_t)regs;
    }

    int64_t n = vfs_read(file->node, file->offset, buf, len);

    // run the int again once a key arrives
    if (n == VFS_AGAIN) {
        process_block_read();
        regs->rip -= INT80_LENGTH;
        return schedule((uint64_t)regs);
    }

    if (n > 0)
        file->offset += n;
    regs->rax = n;
    return (uint64_t)regs;
}

static uint64_t sys_write(struct interrupt_frame *regs) {
    struct file *file = process_file((int)regs->rdi);
    const char *buf = (const char *)regs->rsi;
    uint64_t len = regs->rdx;

    if (file == NULL || len > IO_MAX || !vmm_user_ok(regs->rsi, len, false)) {
        regs->rax = -1;
        return (uint64_t)regs;
    }

    int64_t n = vfs_write(file->node, file->offset, buf, len);
    if (n > 0)
        file->offset += n;
    regs->rax = n;
    return (uint64_t)regs;
}

// the offset of a dir fd is the index of the next entry
static uint64_t sys_readdir(struct interrupt_frame *regs) {
    struct file *file = process_file((int)regs->rdi);
    char *buf = (char *)regs->rsi;
    uint64_t len = regs->rdx;

    if (file == NULL || file->node->type != VFS_DIR || len == 0 || len > IO_MAX
        || !vmm_user_ok(regs->rsi, len, true)) {
        regs->rax = -1;
        return (uint64_t)regs;
    }

    struct vnode *child = vfs_child(file->node, file->offset);
    if (child == NULL) {
        regs->rax = 0;
        return (uint64_t)regs;
    }

    uint64_t n = 0;
    for (; n < len - 1 && child->name[n] != '\0'; n++)
        buf[n] = child->name[n];
    buf[n] = '\0';

    file->offset++;
    regs->rax = n;
    return (uint64_t)regs;
}

uint64_t syscall_handler(struct interrupt_frame *regs) {
    if (regs->rax == SYS_READ) {
        return sys_read(regs);
    } else if (regs->rax == SYS_WRITE) {
        return sys_write(regs);
    } else if (regs->rax == SYS_OPEN) {
        char path[VFS_PATH_MAX];
        regs->rax = copy_user_string(regs->rdi, path, sizeof(path)) ? process_open(path) : -1;
    } else if (regs->rax == SYS_CLOSE) {
        regs->rax = process_close((int)regs->rdi);
    } else if (regs->rax == SYS_CHDIR) {
        char path[VFS_PATH_MAX];
        regs->rax = copy_user_string(regs->rdi, path, sizeof(path)) ? process_chdir(path) : -1;
    } else if (regs->rax == SYS_GETCWD) {
        const char *dir = process_cwd();
        uint64_t len = strlen(dir);

        if (regs->rsi <= len || !vmm_user_ok(regs->rdi, len + 1, true)) {
            regs->rax = -1;
            return (uint64_t)regs;
        }

        memcpy((char *)regs->rdi, dir, len + 1);
        regs->rax = len;
    } else if (regs->rax == SYS_READDIR) {
        return sys_readdir(regs);
    } else if (regs->rax == SYS_YIELD) {
        regs->rax = 0;
        return schedule((uint64_t)regs);
    } else if (regs->rax == SYS_EXIT) {
        process_exit((int)regs->rdi);
        return schedule((uint64_t)regs);
    } else if (regs->rax == SYS_SLEEP) {
        process_sleep(regs->rdi);
        regs->rax = 0;
        return schedule((uint64_t)regs);
    } else if (regs->rax == SYS_SPAWN) {
        char line[ARGS_MAX];
        regs->rax = copy_user_string(regs->rdi, line, sizeof(line)) ? process_spawn(line) : -1;
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
