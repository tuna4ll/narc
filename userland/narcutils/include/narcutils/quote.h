#pragma once

#include <stdbool.h>
#include <stddef.h>

enum quoting_style {
    literal_quoting_style,
    shell_quoting_style,
    shell_always_quoting_style,
    shell_escape_quoting_style,
    shell_escape_always_quoting_style,
    c_quoting_style,
    c_maybe_quoting_style,
    escape_quoting_style,
    locale_quoting_style,
    clocale_quoting_style,
};

enum {
    QA_ELIDE_NULL_BYTES = 1,
    QA_ELIDE_OUTER_QUOTES = 2,
};

struct quoting_options {
    enum quoting_style style;
    int flags;
    unsigned quote_these_too[256 / 32];
};

extern const char *const quoting_style_args[];
extern const enum quoting_style quoting_style_vals[];

void quoting_options_init(struct quoting_options *options, enum quoting_style style);
void set_char_quoting(struct quoting_options *options, char c, bool enable);
size_t quotearg_buffer(char *buffer, size_t size, const char *arg, size_t length,
                       const struct quoting_options *options);
char *quotearg_alloc(const char *arg, size_t length, const struct quoting_options *options,
                     size_t *result_length);
const char *quotearg_n_options(int n, const char *arg, size_t length,
                               const struct quoting_options *options);
const char *quotearg_n_style(int n, enum quoting_style style, const char *arg);
const char *quotearg_style(enum quoting_style style, const char *arg);
const char *quotearg_n_style_colon(int n, enum quoting_style style, const char *arg);
const char *quote_n(int n, const char *arg);
const char *quote(const char *arg);
const char *quote_mem(const char *arg, size_t length);

#define quotef(arg) quotearg_n_style_colon(0, shell_escape_quoting_style, arg)
#define quotef_n(n, arg) quotearg_n_style_colon(n, shell_escape_quoting_style, arg)
#define quoteaf(arg) quotearg_style(shell_escape_always_quoting_style, arg)
#define quoteaf_n(n, arg) quotearg_n_style(n, shell_escape_always_quoting_style, arg)
