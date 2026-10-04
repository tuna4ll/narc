#pragma once
#include <kernel/mm.h>
#include <stddef.h>
#include <stdint.h>

int uaccess_read(struct address_space *space, void *dst, uint64_t src, size_t len);
int uaccess_write(struct address_space *space, uint64_t dst, const void *src, size_t len);
int uaccess_zero(struct address_space *space, uint64_t dst, size_t len);
long uaccess_strlen(struct address_space *space, uint64_t src);
