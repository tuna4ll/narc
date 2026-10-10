#pragma once
#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
int serial_try_getc(char *c);
void serial_rx_enable(void);
void serial_interrupt(void);
void serial_puts(const char *s);
void serial_puthex(uint64_t value);
