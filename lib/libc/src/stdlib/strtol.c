#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

static unsigned long long parse(const char *string, char **end, int base, int *negative,
                                unsigned long long limit) {
    const char *cursor = string;
    while (isspace((unsigned char)*cursor)) cursor++;
    *negative = 0;
    if (*cursor == '+' || *cursor == '-') *negative = *cursor++ == '-';
    if ((base == 0 || base == 16) && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X') &&
        isxdigit((unsigned char)cursor[2])) {
        cursor += 2;
        base = 16;
    } else if (base == 0) {
        base = cursor[0] == '0' ? 8 : 10;
    }
    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (end) *end = (char *)string;
        return 0;
    }
    unsigned long long value = 0;
    int digits = 0, overflow = 0;
    for (;; cursor++, digits++) {
        int c = (unsigned char)*cursor, digit;
        if (isdigit(c)) digit = c - '0';
        else if (isalpha(c)) digit = tolower(c) - 'a' + 10;
        else break;
        if (digit >= base) break;
        if (value > (limit - (unsigned)digit) / (unsigned)base) overflow = 1;
        else value = value * (unsigned)base + (unsigned)digit;
    }
    if (end) *end = (char *)(digits ? cursor : string);
    if (overflow) {
        errno = ERANGE;
        return limit;
    }
    return value;
}

long long strtoll(const char *restrict string, char **restrict end, int base) {
    int negative;
    unsigned long long value = parse(string, end, base, &negative, (unsigned long long)LLONG_MAX + 1);
    if (!negative && value > LLONG_MAX) {
        errno = ERANGE;
        return LLONG_MAX;
    }
    return negative ? (long long)(0 - value) : (long long)value;
}

unsigned long long strtoull(const char *restrict string, char **restrict end, int base) {
    int negative;
    unsigned long long value = parse(string, end, base, &negative, ULLONG_MAX);
    return negative ? 0 - value : value;
}

long strtol(const char *restrict string, char **restrict end, int base) {
    return (long)strtoll(string, end, base);
}

unsigned long strtoul(const char *restrict string, char **restrict end, int base) {
    return (unsigned long)strtoull(string, end, base);
}

intmax_t strtoimax(const char *restrict string, char **restrict end, int base) {
    return strtoll(string, end, base);
}

uintmax_t strtoumax(const char *restrict string, char **restrict end, int base) {
    return strtoull(string, end, base);
}

int atoi(const char *string) { return (int)strtol(string, 0, 10); }
long atol(const char *string) { return strtol(string, 0, 10); }
long long atoll(const char *string) { return strtoll(string, 0, 10); }
