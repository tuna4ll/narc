#ifndef KERNEL_MEM_H
#define KERNEL_MEM_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

#endif /* KERNEL_MEM_H */
