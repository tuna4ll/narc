#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>

size_t strlen(const char *s);
long read(int fd, char *buf, size_t len);
long write(const char *buf, size_t len);
void print(const char *s);
long open(const char *name);
long close(int fd);
long readdir(long index, char *buf, size_t len);
void yield(void);
void sleep(unsigned long ms);
long spawn(const char *line);
long wait(long pid, int *status);
void exit(int code) __attribute__((noreturn));

#endif
