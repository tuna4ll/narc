#include <narcutils/human.h>
#include <narcutils/system.h>

static const char power_letter[] = { 0, 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y', 'R', 'Q' };

static long double adjust_value(int style, long double value) {
    if (style != human_round_to_nearest && value < (long double)UINTMAX_MAX) {
        uintmax_t whole = (uintmax_t)value;
        value = whole + (style == human_ceiling && whole != value);
    }
    return value;
}

char *human_readable(uintmax_t n, char *buffer, int options, uintmax_t from_block_size,
                     uintmax_t to_block_size) {
    int inexact_style = options & (human_round_to_nearest | human_floor | human_ceiling);
    unsigned base = (options & human_base_1024) ? 1024 : 1000;
    uintmax_t amount;
    int tenths, rounding;
    int exponent = -1;
    const int exponent_max = sizeof(power_letter) - 1;
    char *psuffix = buffer + LONGEST_HUMAN_READABLE - 1 - 3;
    char *p = psuffix;
    char *integer_limit;

    if (to_block_size <= from_block_size) {
        if (from_block_size % to_block_size == 0) {
            uintmax_t multiplier = from_block_size / to_block_size;
            amount = n * multiplier;
            if (amount / multiplier == n) {
                tenths = 0;
                rounding = 0;
                goto use_integer_arithmetic;
            }
        }
    } else if (from_block_size != 0 && to_block_size % from_block_size == 0) {
        uintmax_t divisor = to_block_size / from_block_size;
        uintmax_t r10 = (n % divisor) * 10;
        uintmax_t r2 = (r10 % divisor) * 2;
        amount = n / divisor;
        tenths = (int)(r10 / divisor);
        rounding = r2 < divisor ? 0 < r2 : 2 + (divisor < r2);
        goto use_integer_arithmetic;
    }

    {
        long double damount = n * ((long double)from_block_size / to_block_size);
        char temp[LONGEST_HUMAN_READABLE];
        size_t length, nonintegral;
        if (!(options & human_autoscale)) {
            snprintf(temp, sizeof(temp), "%.0Lf", adjust_value(inexact_style, damount));
            length = strlen(temp);
            nonintegral = 0;
        } else {
            long double e = 1;
            exponent = 0;
            do {
                e *= base;
                exponent++;
            } while (e * base <= damount && exponent < exponent_max);
            damount /= e;
            snprintf(temp, sizeof(temp), "%.1Lf", adjust_value(inexact_style, damount * 10) / 10);
            length = strlen(temp);
            nonintegral = 2;
            if (1 + nonintegral + !(options & human_base_1024) < length ||
                ((options & human_suppress_point_zero) && temp[length - 1] == '0')) {
                snprintf(temp, sizeof(temp), "%.0Lf", adjust_value(inexact_style, damount));
                length = strlen(temp);
                nonintegral = 0;
            }
        }
        p = psuffix - length;
        memmove(p, temp, length);
        integer_limit = p + length - nonintegral;
    }
    goto do_grouping;

use_integer_arithmetic:
    if (options & human_autoscale) {
        exponent = 0;
        if (base <= amount) {
            do {
                unsigned r10 = (unsigned)(amount % base) * 10 + (unsigned)tenths;
                unsigned r2 = (r10 % base) * 2 + (unsigned)(rounding >> 1);
                amount /= base;
                tenths = (int)(r10 / base);
                rounding = r2 < base ? (r2 + (unsigned)rounding) != 0
                                     : 2 + (base < r2 + (unsigned)rounding);
                exponent++;
            } while (base <= amount && exponent < exponent_max);

            if (amount < 10) {
                if (inexact_style == human_round_to_nearest ? 2 < rounding + (tenths & 1)
                                                            : inexact_style == human_ceiling &&
                                                                  0 < rounding) {
                    tenths++;
                    rounding = 0;
                    if (tenths == 10) {
                        amount++;
                        tenths = 0;
                    }
                }
                if (amount < 10 && (tenths || !(options & human_suppress_point_zero))) {
                    *--p = (char)('0' + tenths);
                    *--p = '.';
                    tenths = rounding = 0;
                }
            }
        }
    }

    if (inexact_style == human_round_to_nearest
            ? 5 < tenths + (0 < rounding + (int)(amount & 1))
            : inexact_style == human_ceiling && 0 < tenths + rounding) {
        amount++;
        if ((options & human_autoscale) && amount == base && exponent < exponent_max) {
            exponent++;
            if (!(options & human_suppress_point_zero)) {
                *--p = '0';
                *--p = '.';
            }
            amount = 1;
        }
    }

    integer_limit = p;
    do {
        *--p = (char)('0' + amount % 10);
    } while ((amount /= 10) != 0);

do_grouping:
    (void)integer_limit;
    if (options & human_SI) {
        if (exponent < 0) {
            exponent = 0;
            for (uintmax_t power = 1; power < to_block_size; power *= base)
                if (++exponent == exponent_max) break;
        }
        if ((exponent || (options & human_B)) && (options & human_space_before_unit))
            *psuffix++ = ' ';
        if (exponent)
            *psuffix++ = !(options & human_base_1024) && exponent == 1 ? 'k'
                                                                      : power_letter[exponent];
        if (options & human_B) {
            if ((options & human_base_1024) && exponent) *psuffix++ = 'i';
            *psuffix++ = 'B';
        }
    }
    *psuffix = 0;
    return p;
}

static uintmax_t default_block_size(void) {
    return getenv("POSIXLY_CORRECT") ? 512 : 1024;
}

static strtol_error humblock(const char *spec, uintmax_t *block_size, int *options) {
    int flags = 0;
    if (!spec && !(spec = getenv("BLOCK_SIZE")) && !(spec = getenv("BLOCKSIZE"))) {
        *block_size = default_block_size();
    } else {
        if (*spec == '\'') {
            flags |= human_group_digits;
            spec++;
        }
        if (STREQ(spec, "human-readable")) {
            flags |= human_autoscale | human_SI | human_base_1024;
            *block_size = 1;
        } else if (STREQ(spec, "si")) {
            flags |= human_autoscale | human_SI;
            *block_size = 1;
        } else {
            char *end;
            strtol_error status = xstrtoumax(spec, &end, 0, block_size, "eEgGkKmMpPtTyYzZ0");
            if (status != LONGINT_OK) {
                *options = 0;
                return status;
            }
            for (; !('0' <= *spec && *spec <= '9'); spec++) {
                if (spec == end) {
                    flags |= human_SI;
                    if (end[-1] == 'B') flags |= human_B;
                    if (end[-1] != 'B' || end[-2] == 'i') flags |= human_base_1024;
                    break;
                }
            }
        }
    }
    *options = flags;
    return LONGINT_OK;
}

strtol_error human_options(const char *spec, int *options, uintmax_t *block_size) {
    strtol_error status = humblock(spec, block_size, options);
    if (*block_size == 0) {
        *block_size = default_block_size();
        status = LONGINT_INVALID;
    }
    return status;
}
