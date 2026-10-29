#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <limine.h>

/* framebuffer üstünde terminal kurar, ondan önce yazılanlar kaybolur */
void terminal_init(struct limine_framebuffer *fb);

/* imleç konumuna yazar, '\n' alt satıra geçer, ekran dolunca kayar */
void terminal_putchar(char c);

/* null-terminated string yazar, format desteği yok */
void kprint(const char *c);

#endif /* KERNEL_TERMINAL_H */
