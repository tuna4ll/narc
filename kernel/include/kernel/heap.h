#pragma once
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);
