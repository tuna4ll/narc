#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

enum length_kind { LENGTH_INT, LENGTH_LONG, LENGTH_LONG_LONG, LENGTH_SIZE };

static int emit(FILE *stream, const char *data, size_t length, size_t *total) {
    if (length > (size_t)INT_MAX - *total) {
        errno = EOVERFLOW;
        return -1;
    }
    if (fwrite(data, 1, length, stream) != length) return -1;
    *total += length;
    return 0;
}

static int emit_number(FILE *stream, uint64_t value, unsigned int base,
                       int uppercase, int negative, int prefix, int width,
                       int zero_pad, size_t *total) {
    char buffer[32];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t length = 0;
    do {
        buffer[length++] = digits[value % base];
        value /= base;
    } while (value);

    int marker = negative ? 1 : (prefix ? 2 : 0);
    char padding = zero_pad ? '0' : ' ';
    if (!zero_pad)
        while (width > (int)length + marker) {
            if (emit(stream, &padding, 1, total) != 0) return -1;
            width--;
        }
    if (negative && emit(stream, "-", 1, total) != 0) return -1;
    if (prefix && emit(stream, uppercase ? "0X" : "0x", 2, total) != 0) return -1;
    if (zero_pad)
        while (width > (int)length + marker) {
            if (emit(stream, &padding, 1, total) != 0) return -1;
            width--;
        }
    while (length) {
        if (emit(stream, &buffer[--length], 1, total) != 0) return -1;
    }
    return 0;
}

static uint64_t unsigned_argument(va_list *arguments, enum length_kind length) {
    if (length == LENGTH_LONG_LONG) return va_arg(*arguments, unsigned long long);
    if (length == LENGTH_LONG) return va_arg(*arguments, unsigned long);
    if (length == LENGTH_SIZE) return va_arg(*arguments, size_t);
    return va_arg(*arguments, unsigned int);
}

static int64_t signed_argument(va_list *arguments, enum length_kind length) {
    if (length == LENGTH_LONG_LONG) return va_arg(*arguments, long long);
    if (length == LENGTH_LONG) return va_arg(*arguments, long);
    if (length == LENGTH_SIZE) return va_arg(*arguments, ssize_t);
    return va_arg(*arguments, int);
}

int __stdio_format(FILE *stream, const char *format, va_list source) {
    va_list arguments;
    va_copy(arguments, source);
    size_t total = 0;

    while (*format) {
        const char *text = format;
        while (*format && *format != '%') format++;
        if (format != text && emit(stream, text, (size_t)(format - text), &total) != 0)
            goto fail;
        if (!*format) break;
        format++;

        int zero_pad = 0;
        if (*format == '0') {
            zero_pad = 1;
            format++;
        }
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            int digit = *format - '0';
            if (width <= (INT_MAX - digit) / 10) width = width * 10 + digit;
            else width = INT_MAX;
            format++;
        }

        enum length_kind length = LENGTH_INT;
        if (*format == 'l') {
            length = LENGTH_LONG;
            format++;
            if (*format == 'l') {
                length = LENGTH_LONG_LONG;
                format++;
            }
        } else if (*format == 'z') {
            length = LENGTH_SIZE;
            format++;
        }

        char specifier = *format ? *format++ : 0;
        if (specifier == '%') {
            if (emit(stream, "%", 1, &total) != 0) goto fail;
        } else if (specifier == 'c') {
            char value = (char)va_arg(arguments, int);
            if (emit(stream, &value, 1, &total) != 0) goto fail;
        } else if (specifier == 's') {
            const char *value = va_arg(arguments, const char *);
            if (!value) value = "(null)";
            if (emit(stream, value, strlen(value), &total) != 0) goto fail;
        } else if (specifier == 'd' || specifier == 'i') {
            int64_t value = signed_argument(&arguments, length);
            uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
            if (emit_number(stream, magnitude, 10, 0, value < 0, 0, width,
                            zero_pad, &total) != 0) goto fail;
        } else if (specifier == 'u' || specifier == 'o' ||
                   specifier == 'x' || specifier == 'X') {
            unsigned int base = specifier == 'o' ? 8 : (specifier == 'u' ? 10 : 16);
            if (emit_number(stream, unsigned_argument(&arguments, length), base,
                            specifier == 'X', 0, 0, width, zero_pad, &total) != 0)
                goto fail;
        } else if (specifier == 'p') {
            uint64_t value = (uint64_t)(uintptr_t)va_arg(arguments, void *);
            if (emit_number(stream, value, 16, 0, 0, 1, width, zero_pad, &total) != 0)
                goto fail;
        } else {
            errno = EINVAL;
            goto fail;
        }
    }

    va_end(arguments);
    return (int)total;

fail:
    va_end(arguments);
    return -1;
}
