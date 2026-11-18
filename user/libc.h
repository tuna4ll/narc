#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

size_t strlen(const char *s);
long read(int fd, char *buf, size_t len);
long write(int fd, const char *buf, size_t len);
void print(const char *s);
long open(const char *path);
long close(int fd);
long readdir(int fd, char *buf, size_t len);
long chdir(const char *path);
long getcwd(char *buf, size_t len);
void yield(void);
void sleep(unsigned long ms);
long spawn(const char *line);
long wait(long pid, int *status);
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long offset);
long munmap(void *addr, size_t len);
void exit(int code) __attribute__((noreturn));

#endif
