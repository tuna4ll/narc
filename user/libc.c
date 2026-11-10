#include "libc.h"

#define SYS_WRITE 1
#define SYS_YIELD 24
#define SYS_SLEEP 35
#define SYS_SPAWN 59
#define SYS_EXIT  60
#define SYS_WAIT  61

int main(void);

static long syscall2(long num, long arg1, long arg2) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2) : "memory");
    return ret;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

long write(const char *buf, size_t len) {
    return syscall2(SYS_WRITE, (long)buf, (long)len);
}

void print(const char *s) {
    write(s, strlen(s));
}

void yield(void) {
    syscall2(SYS_YIELD, 0, 0);
}

void sleep(unsigned long ms) {
    syscall2(SYS_SLEEP, (long)ms, 0);
}

long spawn(const char *name) {
    return syscall2(SYS_SPAWN, (long)name, 0);
}

long wait(long pid, int *status) {
    return syscall2(SYS_WAIT, pid, (long)status);
}

void exit(int code) {
    syscall2(SYS_EXIT, code, 0);
    for (;;) {
    }
}

__attribute__((section(".text._start")))
void _start(void) {
    exit(main());
}
