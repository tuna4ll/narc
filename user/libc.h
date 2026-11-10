#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>

size_t strlen(const char *s);
long write(const char *buf, size_t len);
void print(const char *s);
void yield(void);
void sleep(unsigned long ms);
long spawn(const char *name);
long wait(long pid, int *status);
void exit(int code) __attribute__((noreturn));

#endif
