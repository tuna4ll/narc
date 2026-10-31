#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <stdint.h>

#include "boot.h"

void terminal_init(const struct boot_framebuffer *fb);

void terminal_putchar(char c);
void kprint(const char *c);
void kprint_hex(uint64_t n);
void kprint_dec(uint64_t n);

#endif /* KERNEL_TERMINAL_H */
