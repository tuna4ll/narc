#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#define FLAG_LEFT  0x01u
#define FLAG_PLUS  0x02u
#define FLAG_SPACE 0x04u
#define FLAG_ALT   0x08u
#define FLAG_ZERO  0x10u

enum length_kind {
    LENGTH_DEFAULT, LENGTH_CHAR, LENGTH_SHORT, LENGTH_LONG, LENGTH_LONG_LONG,
    LENGTH_MAX, LENGTH_SIZE, LENGTH_PTRDIFF, LENGTH_LONG_DOUBLE,
};

struct spec {
    unsigned flags;
    int width;
    int precision;
    enum length_kind length;
    char conversion;
};

static void emit(struct format_sink *sink, const char *data, size_t length) {
    if (!length || sink->failed) return;
    if (length > (size_t)INT_MAX - sink->total) {
        sink->failed = EOVERFLOW;
        return;
    }
    sink->put(sink, data, length);
    sink->total += length;
}

static void pad(struct format_sink *sink, char c, int count) {
    char block[32];
    memset(block, c, sizeof(block));
    while (count > 0) {
        int chunk = count > (int)sizeof(block) ? (int)sizeof(block) : count;
        emit(sink, block, (size_t)chunk);
        count -= chunk;
    }
}

static void emit_field(struct format_sink *sink, const struct spec *spec, const char *prefix,
                       size_t prefix_length, int zeros, const char *body, size_t body_length) {
    int used = (int)(prefix_length + body_length) + zeros;
    int fill = spec->width > used ? spec->width - used : 0;
    if (!(spec->flags & FLAG_LEFT) && !(spec->flags & FLAG_ZERO)) pad(sink, ' ', fill);
    emit(sink, prefix, prefix_length);
    if (!(spec->flags & FLAG_LEFT) && (spec->flags & FLAG_ZERO)) pad(sink, '0', fill);
    pad(sink, '0', zeros);
    emit(sink, body, body_length);
    if (spec->flags & FLAG_LEFT) pad(sink, ' ', fill);
}

static uintmax_t unsigned_argument(va_list *arguments, enum length_kind length) {
    switch (length) {
    case LENGTH_CHAR:      return (unsigned char)va_arg(*arguments, unsigned int);
    case LENGTH_SHORT:     return (unsigned short)va_arg(*arguments, unsigned int);
    case LENGTH_LONG:      return va_arg(*arguments, unsigned long);
    case LENGTH_LONG_LONG: return va_arg(*arguments, unsigned long long);
    case LENGTH_MAX:       return va_arg(*arguments, uintmax_t);
    case LENGTH_SIZE:      return va_arg(*arguments, size_t);
    case LENGTH_PTRDIFF:   return (uintmax_t)va_arg(*arguments, ptrdiff_t);
    default:               return va_arg(*arguments, unsigned int);
    }
}

static intmax_t signed_argument(va_list *arguments, enum length_kind length) {
    switch (length) {
    case LENGTH_CHAR:      return (signed char)va_arg(*arguments, int);
    case LENGTH_SHORT:     return (short)va_arg(*arguments, int);
    case LENGTH_LONG:      return va_arg(*arguments, long);
    case LENGTH_LONG_LONG: return va_arg(*arguments, long long);
    case LENGTH_MAX:       return va_arg(*arguments, intmax_t);
    case LENGTH_SIZE:      return va_arg(*arguments, ssize_t);
    case LENGTH_PTRDIFF:   return va_arg(*arguments, ptrdiff_t);
    default:               return va_arg(*arguments, int);
    }
}

static void format_integer(struct format_sink *sink, struct spec *spec, uintmax_t value,
                           int negative) {
    char digits[72];
    const char *alphabet = spec->conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned base = spec->conversion == 'o' ? 8 :
                    (spec->conversion == 'x' || spec->conversion == 'X' ||
                     spec->conversion == 'p') ? 16 : 10;
    size_t length = 0;
    while (value) {
        digits[sizeof(digits) - ++length] = alphabet[value % base];
        value /= base;
    }
    char prefix[3];
    size_t prefix_length = 0;
    if (negative) prefix[prefix_length++] = '-';
    else if (spec->flags & FLAG_PLUS) prefix[prefix_length++] = '+';
    else if (spec->flags & FLAG_SPACE) prefix[prefix_length++] = ' ';
    int has_value = length > 0;
    if ((spec->flags & FLAG_ALT) && has_value && base == 16) {
        prefix[prefix_length++] = '0';
        prefix[prefix_length++] = spec->conversion == 'X' ? 'X' : 'x';
    }
    int precision = spec->precision;
    if (precision >= 0) spec->flags &= ~FLAG_ZERO;
    else precision = 1;
    int zeros = precision > (int)length ? precision - (int)length : 0;
    if ((spec->flags & FLAG_ALT) && base == 8 && !zeros &&
        (!length || digits[sizeof(digits) - length] != '0'))
        zeros = 1;
    emit_field(sink, spec, prefix, prefix_length, zeros, digits + sizeof(digits) - length, length);
}

static int decimal_digits(double value, int precision, char *out, int capacity, int *exponent) {
    int e = 0;
    if (value != 0) {
        while (value >= 10.0) {
            value /= 10.0;
            e++;
        }
        while (value < 1.0) {
            value *= 10.0;
            e--;
        }
    }
    int count = 0;
    for (; count < precision + 1 && count < capacity; count++) {
        int digit = (int)value;
        if (digit > 9) digit = 9;
        out[count] = (char)('0' + digit);
        value = (value - digit) * 10.0;
    }
    if (value >= 5.0) {
        int i = count - 1;
        while (i >= 0 && out[i] == '9') out[i--] = '0';
        if (i >= 0) {
            out[i]++;
        } else {
            memmove(out + 1, out, (size_t)count - 1);
            out[0] = '1';
            e++;
        }
    }
    *exponent = e;
    return count;
}

static int exact_scaled(double value, int precision, unsigned long long *result) {
    if (precision > 19) return 0;
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    int exponent = (int)((bits >> 52) & 0x7ff);
    uint64_t mantissa = bits & ((UINT64_C(1) << 52) - 1);
    if (exponent) mantissa |= UINT64_C(1) << 52;
    exponent = exponent ? exponent - 1075 : -1074;
    uint64_t power = 1;
    for (int i = 0; i < precision; i++) power *= 10;
    unsigned __int128 scaled = (unsigned __int128)mantissa * power;
    if (exponent >= 0) {
        if (exponent > 10 || (scaled >> (64 - exponent))) return 0;
        *result = (unsigned long long)(scaled << exponent);
        return 1;
    }
    int shift = -exponent;
    if (shift >= 127) {
        *result = 0;
        return 1;
    }
    unsigned __int128 quotient = scaled >> shift;
    unsigned __int128 remainder = scaled - (quotient << shift);
    unsigned __int128 half = (unsigned __int128)1 << (shift - 1);
    if (remainder > half || (remainder == half && (quotient & 1))) quotient++;
    if (quotient >> 64) return 0;
    *result = (unsigned long long)quotient;
    return 1;
}

static size_t fixed_digits(double value, int precision, char *body, int alternate) {
    char digits[400];
    size_t count = 0, length = 0;
    unsigned long long whole;
    if (exact_scaled(value, precision, &whole)) {
        do {
            digits[count++] = (char)('0' + whole % 10);
            whole /= 10;
        } while (whole);
        while (count < (size_t)precision + 1) digits[count++] = '0';
        for (size_t i = count; i > (size_t)precision; i--) body[length++] = digits[i - 1];
        if (precision || alternate) body[length++] = '.';
        for (size_t i = (size_t)precision; i > 0; i--) body[length++] = digits[i - 1];
        return length;
    }
    int exponent;
    int significant = decimal_digits(value, 16, digits, 17, &exponent);
    for (int i = 0; i <= exponent && length < 340; i++)
        body[length++] = i < significant ? digits[i] : '0';
    if (exponent < 0) body[length++] = '0';
    if (precision || alternate) body[length++] = '.';
    for (int i = 0; i < precision; i++) {
        int index = exponent + 1 + i;
        body[length++] = index >= 0 && index < significant ? digits[index] : '0';
    }
    return length;
}

static void format_float(struct format_sink *sink, struct spec *spec, double value) {
    char prefix[2];
    size_t prefix_length = 0;
    int negative = __builtin_signbit(value) != 0;
    if (negative) value = -value;
    if (negative) prefix[prefix_length++] = '-';
    else if (spec->flags & FLAG_PLUS) prefix[prefix_length++] = '+';
    else if (spec->flags & FLAG_SPACE) prefix[prefix_length++] = ' ';
    int upper = spec->conversion >= 'A' && spec->conversion <= 'Z';
    if (value != value || value > 1.7976931348623157e308) {
        spec->flags &= ~FLAG_ZERO;
        const char *text = value != value ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        emit_field(sink, spec, prefix, prefix_length, 0, text, 3);
        return;
    }
    int precision = spec->precision < 0 ? 6 : spec->precision;
    if (precision > 60) precision = 60;
    char conversion = (char)(spec->conversion | 0x20);
    int strip = 0;
    char digits[400];
    int exponent;
    if (conversion == 'g') {
        if (!precision) precision = 1;
        decimal_digits(value, precision - 1, digits, 64, &exponent);
        if (exponent < -4 || exponent >= precision) {
            conversion = 'e';
            precision -= 1;
        } else {
            conversion = 'f';
            precision -= exponent + 1;
        }
        strip = !(spec->flags & FLAG_ALT);
    }
    char body[512];
    size_t length = 0;
    if (conversion == 'e') {
        int count = decimal_digits(value, precision, digits, 64, &exponent);
        body[length++] = digits[0];
        if (precision || (spec->flags & FLAG_ALT)) body[length++] = '.';
        for (int i = 1; i < count; i++) body[length++] = digits[i];
        for (int i = count; i <= precision; i++) body[length++] = '0';
        if (strip && memchr(body, '.', length)) {
            while (body[length - 1] == '0') length--;
            if (body[length - 1] == '.') length--;
        }
        body[length++] = upper ? 'E' : 'e';
        body[length++] = exponent < 0 ? '-' : '+';
        int magnitude = exponent < 0 ? -exponent : exponent;
        if (magnitude >= 100) body[length++] = (char)('0' + magnitude / 100);
        body[length++] = (char)('0' + magnitude / 10 % 10);
        body[length++] = (char)('0' + magnitude % 10);
    } else {
        length = fixed_digits(value, precision, body, (spec->flags & FLAG_ALT) != 0);
        if (strip && memchr(body, '.', length)) {
            while (body[length - 1] == '0') length--;
            if (body[length - 1] == '.') length--;
        }
    }
    emit_field(sink, spec, prefix, prefix_length, 0, body, length);
}

static const char *parse_spec(const char *format, struct spec *spec, va_list *arguments) {
    spec->flags = 0;
    for (;; format++) {
        if (*format == '-') spec->flags |= FLAG_LEFT;
        else if (*format == '+') spec->flags |= FLAG_PLUS;
        else if (*format == ' ') spec->flags |= FLAG_SPACE;
        else if (*format == '#') spec->flags |= FLAG_ALT;
        else if (*format == '0') spec->flags |= FLAG_ZERO;
        else break;
    }
    spec->width = 0;
    if (*format == '*') {
        spec->width = va_arg(*arguments, int);
        if (spec->width < 0) {
            spec->flags |= FLAG_LEFT;
            spec->width = spec->width == INT_MIN ? INT_MAX : -spec->width;
        }
        format++;
    } else {
        for (; *format >= '0' && *format <= '9'; format++)
            spec->width = spec->width > (INT_MAX - 9) / 10 ? INT_MAX :
                          spec->width * 10 + (*format - '0');
    }
    spec->precision = -1;
    if (*format == '.') {
        format++;
        spec->precision = 0;
        if (*format == '*') {
            spec->precision = va_arg(*arguments, int);
            if (spec->precision < 0) spec->precision = -1;
            format++;
        } else {
            for (; *format >= '0' && *format <= '9'; format++)
                spec->precision = spec->precision > (INT_MAX - 9) / 10 ? INT_MAX :
                                  spec->precision * 10 + (*format - '0');
        }
    }
    if (spec->flags & FLAG_LEFT) spec->flags &= ~FLAG_ZERO;
    spec->length = LENGTH_DEFAULT;
    switch (*format) {
    case 'h':
        spec->length = format[1] == 'h' ? LENGTH_CHAR : LENGTH_SHORT;
        format += format[1] == 'h' ? 2 : 1;
        break;
    case 'l':
        spec->length = format[1] == 'l' ? LENGTH_LONG_LONG : LENGTH_LONG;
        format += format[1] == 'l' ? 2 : 1;
        break;
    case 'j': spec->length = LENGTH_MAX; format++; break;
    case 'z': spec->length = LENGTH_SIZE; format++; break;
    case 't': spec->length = LENGTH_PTRDIFF; format++; break;
    case 'L': spec->length = LENGTH_LONG_DOUBLE; format++; break;
    default: break;
    }
    spec->conversion = *format;
    return *format ? format + 1 : format;
}

int __stdio_format(struct format_sink *sink, const char *format, va_list source) {
    va_list arguments;
    va_copy(arguments, source);
    while (*format && !sink->failed) {
        const char *text = format;
        while (*format && *format != '%') format++;
        emit(sink, text, (size_t)(format - text));
        if (!*format) break;
        struct spec spec;
        format = parse_spec(format + 1, &spec, &arguments);
        switch (spec.conversion) {
        case '%':
            emit(sink, "%", 1);
            break;
        case 'c': {
            char c = (char)va_arg(arguments, int);
            spec.flags &= ~FLAG_ZERO;
            emit_field(sink, &spec, 0, 0, 0, &c, 1);
            break;
        }
        case 's': {
            const char *value = va_arg(arguments, const char *);
            if (!value) value = "(null)";
            size_t length = spec.precision >= 0 ? strnlen(value, (size_t)spec.precision) :
                                                  strlen(value);
            spec.flags &= ~FLAG_ZERO;
            emit_field(sink, &spec, 0, 0, 0, value, length);
            break;
        }
        case 'd':
        case 'i': {
            intmax_t value = signed_argument(&arguments, spec.length);
            uintmax_t magnitude = value < 0 ? (uintmax_t)0 - (uintmax_t)value : (uintmax_t)value;
            format_integer(sink, &spec, magnitude, value < 0);
            break;
        }
        case 'u': case 'o': case 'x': case 'X':
            spec.flags &= ~(FLAG_PLUS | FLAG_SPACE);
            format_integer(sink, &spec, unsigned_argument(&arguments, spec.length), 0);
            break;
        case 'p':
            spec.flags = (spec.flags & ~(FLAG_PLUS | FLAG_SPACE)) | FLAG_ALT;
            format_integer(sink, &spec, (uintptr_t)va_arg(arguments, void *), 0);
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double value = spec.length == LENGTH_LONG_DOUBLE ?
                           (double)va_arg(arguments, long double) : va_arg(arguments, double);
            format_float(sink, &spec, value);
            break;
        }
        case 'n': {
            int *count = va_arg(arguments, int *);
            if (count) *count = (int)sink->total;
            break;
        }
        default:
            sink->failed = EINVAL;
            break;
        }
    }
    va_end(arguments);
    if (sink->failed) {
        errno = sink->failed;
        return -1;
    }
    return (int)sink->total;
}
