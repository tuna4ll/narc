#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include "boot.h"

void terminal_init(const struct boot_framebuffer *fb);

void terminal_putchar(char c);
void kprint(const char *c);

#endif /* KERNEL_TERMINAL_H */
