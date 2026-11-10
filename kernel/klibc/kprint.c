#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "kprint.h"
#include "terminal.h"
#include "cpu.h"

static void put_padded(const char *s, int len, int width, bool left, char pad) {
    if (!left)
        for (int i = len; i < width; i++)
            terminal_putchar(pad);
    for (int i = 0; i < len; i++)
        terminal_putchar(s[i]);
    if (left)
        for (int i = len; i < width; i++)
            terminal_putchar(' ');
}

static void put_number(uint64_t n, unsigned base, bool upper, bool negative,
                       int width, bool left, char pad) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[21];
    int i = sizeof(buf);

    do {
        buf[--i] = digits[n % base];
        n /= base;
    } while (n > 0);

    if (negative) {
        if (pad == '0') {
            terminal_putchar('-');
            width--;
        } else {
            buf[--i] = '-';
        }
    }

    put_padded(&buf[i], sizeof(buf) - i, width, left, pad);
}

static void vkprint(const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            terminal_putchar(*fmt);
            continue;
        }
        fmt++;

        bool left = false;
        char pad = ' ';
        for (;; fmt++) {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }
        if (left)
            pad = ' ';

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        bool is_long = false;
        while (*fmt == 'l' || *fmt == 'z') {
            is_long = true;
            fmt++;
        }

        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v = is_long ? va_arg(ap, int64_t) : va_arg(ap, int);
            uint64_t u = v < 0 ? -(uint64_t)v : (uint64_t)v;
            put_number(u, 10, false, v < 0, width, left, pad);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t v = is_long ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            put_number(v, *fmt == 'u' ? 10 : 16, *fmt == 'X', false, width, left, pad);
            break;
        }
        case 'p':
            terminal_putchar('0');
            terminal_putchar('x');
            put_number((uintptr_t)va_arg(ap, void *), 16, false, false, 16, false, '0');
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = 0;
            while (s[len])
                len++;
            put_padded(s, len, width, left, ' ');
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            put_padded(&c, 1, width, left, ' ');
            break;
        }
        case '%':
            terminal_putchar('%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            terminal_putchar('%');
            terminal_putchar(*fmt);
            break;
        }
    }
}

void kprint(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vkprint(fmt, ap);
    va_end(ap);
}

void panic(const char *fmt, ...) {
    static bool panicking = false;

    __asm__ volatile ("cli");

    if (!panicking) {
        panicking = true;

        va_list ap;
        va_start(ap, fmt);
        kprint("\nKERNEL PANIC: ");
        vkprint(fmt, ap);
        kprint("\n");
        va_end(ap);
    }

    khalt();
}
