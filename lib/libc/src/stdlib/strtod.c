#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

static long double scale10(long double value, long exponent) {
    long double factor = 10.0L, result = 1.0L;
    unsigned long magnitude = exponent < 0 ? (unsigned long)-exponent : (unsigned long)exponent;
    while (magnitude) {
        if (magnitude & 1) result *= factor;
        factor *= factor;
        magnitude >>= 1;
    }
    return exponent < 0 ? value / result : value * result;
}

static long double scale2(long double value, long exponent) {
    while (exponent > 0) {
        long step = exponent > 60 ? 60 : exponent;
        value *= (long double)(1ULL << step);
        exponent -= step;
    }
    while (exponent < 0) {
        long step = -exponent > 60 ? 60 : -exponent;
        value /= (long double)(1ULL << step);
        exponent += step;
    }
    return value;
}

static long parse_exponent(const char **cursor, char marker_lower, char marker_upper) {
    const char *s = *cursor;
    if (*s != marker_lower && *s != marker_upper) return 0;
    s++;
    int negative = 0;
    if (*s == '+' || *s == '-') negative = *s++ == '-';
    if (!isdigit((unsigned char)*s)) return 0;
    long value = 0;
    while (isdigit((unsigned char)*s)) {
        if (value < 100000) value = value * 10 + (*s - '0');
        s++;
    }
    *cursor = s;
    return negative ? -value : value;
}

static long double parse_hex(const char **cursor, int *valid) {
    const char *s = *cursor;
    uint64_t mantissa = 0;
    long exponent = 0;
    int digits = 0;
    for (int fraction = 0;; s++) {
        if (*s == '.' && !fraction) {
            fraction = 1;
            continue;
        }
        if (!isxdigit((unsigned char)*s)) break;
        int digit = isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10);
        digits++;
        if (mantissa >> 60) {
            if (!fraction) exponent += 4;
            continue;
        }
        mantissa = mantissa << 4 | (uint64_t)digit;
        if (fraction) exponent -= 4;
    }
    *valid = digits > 0;
    if (!digits) return 0;
    exponent += parse_exponent(&s, 'p', 'P');
    *cursor = s;
    return scale2((long double)mantissa, exponent);
}

static long double parse_decimal(const char **cursor, int *valid) {
    const char *s = *cursor;
    uint64_t mantissa = 0;
    long exponent = 0;
    int digits = 0, significant = 0;
    for (int fraction = 0;; s++) {
        if (*s == '.' && !fraction) {
            fraction = 1;
            continue;
        }
        if (!isdigit((unsigned char)*s)) break;
        digits++;
        if (significant < 19) {
            if (mantissa || *s != '0') significant++;
            mantissa = mantissa * 10 + (uint64_t)(*s - '0');
            if (fraction) exponent--;
        } else if (!fraction) {
            exponent++;
        }
    }
    *valid = digits > 0;
    if (!digits) return 0;
    exponent += parse_exponent(&s, 'e', 'E');
    *cursor = s;
    return mantissa ? scale10((long double)mantissa, exponent) : 0.0L;
}

long double strtold(const char *restrict string, char **restrict end) {
    const char *s = string;
    while (isspace((unsigned char)*s)) s++;
    int negative = 0;
    if (*s == '+' || *s == '-') negative = *s++ == '-';
    long double value;
    int valid = 1;
    if (!strncasecmp(s, "infinity", 8)) {
        value = HUGE_VALL;
        s += 8;
    } else if (!strncasecmp(s, "inf", 3)) {
        value = HUGE_VALL;
        s += 3;
    } else if (!strncasecmp(s, "nan", 3)) {
        value = __builtin_nanl("");
        s += 3;
        if (*s == '(') {
            const char *close = s + 1;
            while (isalnum((unsigned char)*close) || *close == '_') close++;
            if (*close == ')') s = close + 1;
        }
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        const char *digits = s + 2;
        value = parse_hex(&digits, &valid);
        s = valid ? digits : s + 1;
        valid = 1;
    } else {
        value = parse_decimal(&s, &valid);
    }
    if (!valid) {
        if (end) *end = (char *)string;
        return 0;
    }
    if (end) *end = (char *)s;
    return negative ? -value : value;
}

double strtod(const char *restrict string, char **restrict end) {
    long double value = strtold(string, end);
    if (isfinite(value) && fabsl(value) > DBL_MAX) {
        errno = ERANGE;
        return value < 0 ? -HUGE_VAL : HUGE_VAL;
    }
    if (value != 0 && fabsl(value) < DBL_MIN) errno = ERANGE;
    return (double)value;
}

float strtof(const char *restrict string, char **restrict end) {
    long double value = strtold(string, end);
    if (isfinite(value) && fabsl(value) > FLT_MAX) {
        errno = ERANGE;
        return value < 0 ? -HUGE_VALF : HUGE_VALF;
    }
    return (float)value;
}

double atof(const char *string) {
    return strtod(string, 0);
}
