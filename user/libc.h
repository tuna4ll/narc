#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>

size_t strlen(const char *s);
long write(const char *buf, size_t len);
void print(const char *s);
void exit(int code) __attribute__((noreturn));

#endif
