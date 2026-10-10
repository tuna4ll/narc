#include <kernel/mm.h>
#include <kernel/serial.h>
#include <kernel/tty.h>

#if defined(__x86_64__)
#include <kernel/io.h>
#define COM1 0x3f8
#elif defined(__aarch64__)
#define UART_BASE 0x09000000ULL
static volatile uint32_t *uart;
#else
#define UART_BASE 0x10000000ULL
static volatile uint8_t *uart;
#endif

void serial_init(void) {
#if defined(__x86_64__)
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
#elif defined(__aarch64__)
    vmm_map_kernel((uint64_t)(uintptr_t)phys_to_virt(UART_BASE), UART_BASE,
                   VMM_WRITE | VMM_DEVICE);
    uart = phys_to_virt(UART_BASE);
#else
    vmm_map_kernel((uint64_t)(uintptr_t)phys_to_virt(UART_BASE), UART_BASE,
                   VMM_WRITE | VMM_DEVICE);
    uart = phys_to_virt(UART_BASE);
#endif
}

void serial_putc(char c) {
#if defined(__x86_64__)
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, (uint8_t)c);
#elif defined(__aarch64__)
    while (uart[6] & (1U << 5)) {}
    uart[0] = (uint32_t)c;
#else
    register uint64_t a0 __asm__("a0") = (uint8_t)c;
    register uint64_t a7 __asm__("a7") = 1;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7) : "memory");
#endif
}

int serial_try_getc(char *c) {
#if defined(__x86_64__)
    if (!(inb(COM1 + 5) & 1)) return 0;
    *c = (char)inb(COM1);
#elif defined(__aarch64__)
    if (uart[6] & (1U << 4)) return 0;
    *c = (char)uart[0];
#else
    if (!(uart[5] & 1)) return 0;
    *c = (char)uart[0];
#endif
    return 1;
}

void serial_rx_enable(void) {
#if defined(__x86_64__)
    outb(COM1 + 1, 0x01);
    outb(COM1 + 4, 0x0b);
#elif defined(__aarch64__)
    uart[0x38 / 4] |= (1U << 4) | (1U << 6);
#else
    uart[1] = 0x01;
    uart[4] |= 0x08;
#endif
}

void serial_interrupt(void) {
    char c;
    while (serial_try_getc(&c)) tty_input(c);
#if defined(__aarch64__)
    uart[0x44 / 4] = (1U << 4) | (1U << 6);
#endif
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

void serial_puthex(uint64_t value) {
    static const char hex[] = "0123456789abcdef";
    serial_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(hex[(value >> shift) & 0xf]);
}
