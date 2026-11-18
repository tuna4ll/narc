#include "libc.h"

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_MMAP    9
#define SYS_MUNMAP  11
#define SYS_YIELD   24
#define SYS_SLEEP   35
#define SYS_SPAWN   59
#define SYS_EXIT    60
#define SYS_WAIT    61
#define SYS_GETCWD  79
#define SYS_CHDIR   80
#define SYS_READDIR 89

int main(int argc, char **argv);

static long syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3) : "memory");
    return ret;
}

static long syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "memory");
    return ret;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, long offset) {
    return (void *)syscall6(SYS_MMAP, (long)addr, (long)len, prot, flags, fd, offset);
}

long munmap(void *addr, size_t len) {
    return syscall3(SYS_MUNMAP, (long)addr, (long)len, 0);
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

long read(int fd, char *buf, size_t len) {
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}

long write(int fd, const char *buf, size_t len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

void print(const char *s) {
    write(1, s, strlen(s));
}

long open(const char *path) {
    return syscall3(SYS_OPEN, (long)path, 0, 0);
}

long close(int fd) {
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

long readdir(int fd, char *buf, size_t len) {
    return syscall3(SYS_READDIR, fd, (long)buf, (long)len);
}

long chdir(const char *path) {
    return syscall3(SYS_CHDIR, (long)path, 0, 0);
}

long getcwd(char *buf, size_t len) {
    return syscall3(SYS_GETCWD, (long)buf, (long)len, 0);
}

void yield(void) {
    syscall3(SYS_YIELD, 0, 0, 0);
}

void sleep(unsigned long ms) {
    syscall3(SYS_SLEEP, (long)ms, 0, 0);
}

long spawn(const char *line) {
    return syscall3(SYS_SPAWN, (long)line, 0, 0);
}

long wait(long pid, int *status) {
    return syscall3(SYS_WAIT, pid, (long)status, 0);
}

void exit(int code) {
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;) {
    }
}

// the kernel puts argc and argv in rdi and rsi
__attribute__((section(".text._start")))
void _start(int argc, char **argv) {
    exit(main(argc, argv));
}
