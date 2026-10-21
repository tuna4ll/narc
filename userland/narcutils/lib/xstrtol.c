#include <ctype.h>
#include <inttypes.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <narcutils/xstrtol.h>

static strtol_error scale(uintmax_t *value, unsigned factor) {
    if (*value > UINTMAX_MAX / factor) {
        *value = UINTMAX_MAX;
        return LONGINT_OVERFLOW;
    }
    *value *= factor;
    return LONGINT_OK;
}

static strtol_error scale_power(uintmax_t *value, unsigned base, int power) {
    strtol_error status = LONGINT_OK;
    while (power--) status |= scale(value, base);
    return status;
}

static strtol_error apply_suffix(uintmax_t *value, char **end, const char *valid_suffixes) {
    char *p = *end;
    if (!*p) return LONGINT_OK;
    if (!strchr(valid_suffixes, *p)) return LONGINT_INVALID_SUFFIX_CHAR;
    unsigned base = 1024;
    int suffixes = 1;
    if (strchr("EGgkKMmPQRTtYZ", *p) && strchr(valid_suffixes, '0')) {
        if (p[1] == 'i' && p[2] == 'B') suffixes += 2;
        else if (p[1] == 'B' || p[1] == 'D') {
            base = 1000;
            suffixes++;
        }
    }
    strtol_error status;
    switch (*p) {
    case 'b': status = scale(value, 512); break;
    case 'B': status = scale(value, 1024); break;
    case 'c': status = LONGINT_OK; break;
    case 'E': status = scale_power(value, base, 6); break;
    case 'G': case 'g': status = scale_power(value, base, 3); break;
    case 'k': case 'K': status = scale_power(value, base, 1); break;
    case 'M': case 'm': status = scale_power(value, base, 2); break;
    case 'P': status = scale_power(value, base, 5); break;
    case 'Q': status = scale_power(value, base, 10); break;
    case 'R': status = scale_power(value, base, 9); break;
    case 'T': case 't': status = scale_power(value, base, 4); break;
    case 'w': status = scale(value, 2); break;
    case 'Y': status = scale_power(value, base, 8); break;
    case 'Z': status = scale_power(value, base, 7); break;
    default: return LONGINT_INVALID_SUFFIX_CHAR;
    }
    *end = p + suffixes;
    if (**end) status |= LONGINT_INVALID_SUFFIX_CHAR;
    return status;
}

strtol_error xstrtoumax(const char *string, char **end, int base, uintmax_t *value,
                        const char *valid_suffixes) {
    char *local_end;
    char **p = end ? end : &local_end;
    const char *q = string;
    while (isspace((unsigned char)*q)) q++;
    if (*q == '-') {
        *p = (char *)string;
        return LONGINT_INVALID;
    }
    errno = 0;
    uintmax_t number = strtoumax(string, p, base);
    strtol_error status = LONGINT_OK;
    if (*p == string) {
        if (valid_suffixes && **p && strchr(valid_suffixes, **p)) number = 1;
        else return LONGINT_INVALID;
    } else if (errno == ERANGE) {
        status = LONGINT_OVERFLOW;
    } else if (errno) {
        return LONGINT_INVALID;
    }
    if (!valid_suffixes) {
        *value = number;
        return status;
    }
    status |= apply_suffix(&number, p, valid_suffixes);
    *value = number;
    return status;
}

strtol_error xstrtoimax(const char *string, char **end, int base, intmax_t *value,
                        const char *valid_suffixes) {
    const char *q = string;
    while (isspace((unsigned char)*q)) q++;
    bool negative = *q == '-';
    uintmax_t magnitude;
    char *local_end;
    char **p = end ? end : &local_end;
    strtol_error status;
    if (negative) {
        status = xstrtoumax(q + 1, p, base, &magnitude, valid_suffixes);
        if (status == LONGINT_INVALID) {
            *p = (char *)string;
            return status;
        }
        if (q[1] == '-' || q[1] == '+' || isspace((unsigned char)q[1])) {
            *p = (char *)string;
            return LONGINT_INVALID;
        }
        if (magnitude > (uintmax_t)INTMAX_MAX + 1) {
            *value = INTMAX_MIN;
            return status | LONGINT_OVERFLOW;
        }
        *value = magnitude == (uintmax_t)INTMAX_MAX + 1 ? INTMAX_MIN : -(intmax_t)magnitude;
        return status;
    }
    status = xstrtoumax(string, p, base, &magnitude, valid_suffixes);
    if (status == LONGINT_INVALID) return status;
    if (magnitude > INTMAX_MAX) {
        *value = INTMAX_MAX;
        return status | LONGINT_OVERFLOW;
    }
    *value = (intmax_t)magnitude;
    return status;
}

uintmax_t xnumtoumax(const char *string, int base, uintmax_t min, uintmax_t max,
                     const char *suffixes, const char *error_message, int error_status) {
    uintmax_t value;
    strtol_error status = xstrtoumax(string, 0, base, &value, suffixes);
    int errnum = 0;
    if (status == LONGINT_OK) {
        if (value >= min && value <= max) return value;
        errnum = value > INT_MAX / 2 ? EOVERFLOW : ERANGE;
    } else if (status == LONGINT_OVERFLOW) {
        errnum = EOVERFLOW;
    }
    die(error_status ? error_status : EXIT_FAILURE, errnum, "%s: %s", error_message,
        quote(string));
}

intmax_t xnumtoimax(const char *string, int base, intmax_t min, intmax_t max,
                    const char *suffixes, const char *error_message, int error_status) {
    intmax_t value;
    strtol_error status = xstrtoimax(string, 0, base, &value, suffixes);
    int errnum = 0;
    if (status == LONGINT_OK) {
        if (value >= min && value <= max) return value;
        errnum = (value < INT_MIN / 2 || value > INT_MAX / 2) ? EOVERFLOW : ERANGE;
    } else if (status == LONGINT_OVERFLOW) {
        errnum = EOVERFLOW;
    }
    die(error_status ? error_status : EXIT_FAILURE, errnum, "%s: %s", error_message,
        quote(string));
}

uintmax_t xdectoumax(const char *string, uintmax_t min, uintmax_t max, const char *suffixes,
                     const char *error_message, int error_status) {
    return xnumtoumax(string, 10, min, max, suffixes, error_message, error_status);
}

intmax_t xdectoimax(const char *string, intmax_t min, intmax_t max, const char *suffixes,
                    const char *error_message, int error_status) {
    return xnumtoimax(string, 10, min, max, suffixes, error_message, error_status);
}

void xstrtol_fatal(strtol_error status, int option_index, char option,
                   const void *long_options_pointer, const char *arg) {
    const struct option *long_options = long_options_pointer;
    const char *hyphens = "--";
    const char *message;
    char short_option[2];
    const char *option_name;
    if (option_index < 0) {
        hyphens += 1;
        short_option[0] = option;
        short_option[1] = 0;
        option_name = short_option;
    } else {
        option_name = long_options[option_index].name;
    }
    switch (status) {
    case LONGINT_INVALID_SUFFIX_CHAR:
    case LONGINT_INVALID_SUFFIX_CHAR_WITH_OVERFLOW:
        message = "invalid suffix in %s%s argument '%s'";
        break;
    case LONGINT_OVERFLOW:
        message = "%s%s argument '%s' too large";
        break;
    default:
        message = "invalid %s%s argument '%s'";
        break;
    }
    die(EXIT_FAILURE, 0, message, hyphens, option_name, arg);
}
