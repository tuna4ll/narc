#pragma once

#include <stdint.h>

typedef enum {
    LONGINT_OK = 0,
    LONGINT_OVERFLOW = 1,
    LONGINT_INVALID_SUFFIX_CHAR = 2,
    LONGINT_INVALID_SUFFIX_CHAR_WITH_OVERFLOW = 3,
    LONGINT_INVALID = 4,
} strtol_error;

strtol_error xstrtoumax(const char *string, char **end, int base, uintmax_t *value,
                        const char *valid_suffixes);
strtol_error xstrtoimax(const char *string, char **end, int base, intmax_t *value,
                        const char *valid_suffixes);
uintmax_t xnumtoumax(const char *string, int base, uintmax_t min, uintmax_t max,
                     const char *suffixes, const char *error_message, int error_status);
intmax_t xnumtoimax(const char *string, int base, intmax_t min, intmax_t max,
                    const char *suffixes, const char *error_message, int error_status);
uintmax_t xdectoumax(const char *string, uintmax_t min, uintmax_t max, const char *suffixes,
                     const char *error_message, int error_status);
intmax_t xdectoimax(const char *string, intmax_t min, intmax_t max, const char *suffixes,
                    const char *error_message, int error_status);
void xstrtol_fatal(strtol_error error, int option_index, char option,
                   const void *long_options, const char *arg);
