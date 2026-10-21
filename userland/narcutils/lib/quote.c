#include <ctype.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>

const char *const quoting_style_args[] = {
    "literal", "shell", "shell-always", "shell-escape", "shell-escape-always",
    "c", "c-maybe", "escape", "locale", "clocale", 0,
};

const enum quoting_style quoting_style_vals[] = {
    literal_quoting_style, shell_quoting_style, shell_always_quoting_style,
    shell_escape_quoting_style, shell_escape_always_quoting_style, c_quoting_style,
    c_maybe_quoting_style, escape_quoting_style, locale_quoting_style, clocale_quoting_style,
};

struct output {
    char *data;
    size_t length, capacity;
};

static void put(struct output *out, char c) {
    if (out->length + 1 >= out->capacity) {
        out->capacity = out->capacity ? out->capacity * 2 : 64;
        out->data = xrealloc(out->data, out->capacity);
    }
    out->data[out->length++] = c;
}

static void put_string(struct output *out, const char *s) {
    while (*s) put(out, *s++);
}

void quoting_options_init(struct quoting_options *options, enum quoting_style style) {
    memset(options, 0, sizeof(*options));
    options->style = style;
}

void set_char_quoting(struct quoting_options *options, char c, bool enable) {
    unsigned char u = (unsigned char)c;
    unsigned bit = 1u << (u % 32);
    if (enable) options->quote_these_too[u / 32] |= bit;
    else options->quote_these_too[u / 32] &= ~bit;
}

enum restyle { RESTYLE_NONE, RESTYLE_FORCE_OUTER, RESTYLE_C };

static enum restyle quote_pass(struct output *out, const char *arg, size_t size,
                               enum quoting_style *style_io, int flags,
                               const unsigned *quote_these_too) {
    enum quoting_style style = *style_io;
    bool backslash_escapes = false;
    bool elide_outer_quotes = (flags & QA_ELIDE_OUTER_QUOTES) != 0;
    bool pending_shell_escape_end = false;
    bool encountered_single_quote = false;
    bool all_compat = true;
    const char *quote_string = 0;
    size_t quote_length = 0;

    switch (style) {
    case c_maybe_quoting_style:
        style = c_quoting_style;
        elide_outer_quotes = true;
        /* fallthrough */
    case c_quoting_style:
        if (!elide_outer_quotes) put(out, '"');
        backslash_escapes = true;
        quote_string = "\"";
        quote_length = 1;
        break;
    case escape_quoting_style:
        backslash_escapes = true;
        elide_outer_quotes = false;
        break;
    case locale_quoting_style:
    case clocale_quoting_style: {
        const char *quote_char = style == clocale_quoting_style ? "\"" : "'";
        if (!elide_outer_quotes) put_string(out, quote_char);
        backslash_escapes = true;
        quote_string = quote_char;
        quote_length = 1;
        break;
    }
    case shell_escape_quoting_style:
        backslash_escapes = true;
        /* fallthrough */
    case shell_quoting_style:
        elide_outer_quotes = true;
        /* fallthrough */
    case shell_escape_always_quoting_style:
        if (!elide_outer_quotes) backslash_escapes = true;
        /* fallthrough */
    case shell_always_quoting_style:
        style = shell_always_quoting_style;
        if (!elide_outer_quotes) put(out, '\'');
        quote_string = "'";
        quote_length = 1;
        break;
    case literal_quoting_style:
        elide_outer_quotes = false;
        break;
    }
    *style_io = style;

#define FORCE_OUTER()                                                          \
    do {                                                                       \
        if (style == shell_always_quoting_style && backslash_escapes)          \
            *style_io = shell_escape_always_quoting_style;                     \
        return RESTYLE_FORCE_OUTER;                                            \
    } while (0)
#define START_ESC()                                                            \
    do {                                                                       \
        if (elide_outer_quotes) FORCE_OUTER();                                 \
        escaping = true;                                                       \
        if (style == shell_always_quoting_style && !pending_shell_escape_end) { \
            put_string(out, "'$'");                                            \
            pending_shell_escape_end = true;                                   \
        }                                                                      \
        put(out, '\\');                                                        \
    } while (0)
#define END_ESC()                                                              \
    do {                                                                       \
        if (pending_shell_escape_end && !escaping) {                           \
            put_string(out, "''");                                             \
            pending_shell_escape_end = false;                                  \
        }                                                                      \
    } while (0)

    for (size_t i = 0; i < size; i++) {
        unsigned char esc = 0;
        bool is_right_quote = false;
        bool escaping = false;
        bool compat = false;

        if (backslash_escapes && style != shell_always_quoting_style && quote_length &&
            i + quote_length <= size && !memcmp(arg + i, quote_string, quote_length)) {
            if (elide_outer_quotes) FORCE_OUTER();
            is_right_quote = true;
        }

        unsigned char c = (unsigned char)arg[i];
        switch (c) {
        case '\0':
            if (backslash_escapes) {
                START_ESC();
                if (style != shell_always_quoting_style && i + 1 < size && arg[i + 1] >= '0' &&
                    arg[i + 1] <= '9') {
                    put(out, '0');
                    put(out, '0');
                }
                c = '0';
            } else if (flags & QA_ELIDE_NULL_BYTES) {
                continue;
            }
            break;
        case '?':
            if (style == shell_always_quoting_style && elide_outer_quotes) FORCE_OUTER();
            break;
        case '\a': esc = 'a'; goto c_escape;
        case '\b': esc = 'b'; goto c_escape;
        case '\f': esc = 'f'; goto c_escape;
        case '\n': esc = 'n'; goto c_and_shell_escape;
        case '\r': esc = 'r'; goto c_and_shell_escape;
        case '\t': esc = 't'; goto c_and_shell_escape;
        case '\v': esc = 'v'; goto c_escape;
        case '\\':
            esc = c;
            if (style == shell_always_quoting_style) {
                if (elide_outer_quotes) FORCE_OUTER();
                goto store_c;
            }
            if (backslash_escapes && elide_outer_quotes && quote_length) goto store_c;
        c_and_shell_escape:
            if (style == shell_always_quoting_style && elide_outer_quotes) FORCE_OUTER();
        c_escape:
            if (backslash_escapes) {
                c = esc;
                goto store_escape;
            }
            break;
        case '{': case '}':
            if (size != 1) break;
            /* fallthrough */
        case '#': case '~':
            if (i != 0) break;
            /* fallthrough */
        case ' ':
            compat = true;
            /* fallthrough */
        case '!': case '"': case '$': case '&': case '(': case ')': case '*': case ';':
        case '<': case '=': case '>': case '[': case '^': case '`': case '|':
            if (style == shell_always_quoting_style && elide_outer_quotes) FORCE_OUTER();
            break;
        case '\'':
            encountered_single_quote = true;
            compat = true;
            if (style == shell_always_quoting_style) {
                if (elide_outer_quotes) FORCE_OUTER();
                put_string(out, "'\\'");
                pending_shell_escape_end = false;
            }
            break;
        case '%': case '+': case ',': case '-': case '.': case '/': case ':': case ']':
        case '_':
            compat = true;
            break;
        default:
            if (isalnum(c)) {
                compat = true;
                break;
            }
            if (isprint(c)) {
                compat = true;
                break;
            }
            if (backslash_escapes) {
                START_ESC();
                put(out, (char)('0' + (c >> 6)));
                put(out, (char)('0' + ((c >> 3) & 7)));
                c = (unsigned char)('0' + (c & 7));
                goto store_c;
            }
            break;
        }

        if (!(((backslash_escapes && style != shell_always_quoting_style) || elide_outer_quotes) &&
              quote_these_too && (quote_these_too[c / 32] >> (c % 32) & 1)) &&
            !is_right_quote)
            goto store_c;
    store_escape:
        START_ESC();
    store_c:
        END_ESC();
        put(out, (char)c);
        if (!compat) all_compat = false;
    }

    if (!size && style == shell_always_quoting_style && elide_outer_quotes) FORCE_OUTER();
    if (style == shell_always_quoting_style && !elide_outer_quotes && encountered_single_quote &&
        all_compat)
        return RESTYLE_C;
    if (quote_string && !elide_outer_quotes) put_string(out, quote_string);
    return RESTYLE_NONE;
#undef FORCE_OUTER
#undef START_ESC
#undef END_ESC
}

static void quote_into(struct output *out, const char *arg, size_t size, enum quoting_style style,
                       int flags, const unsigned *quote_these_too) {
    for (;;) {
        out->length = 0;
        enum quoting_style current = style;
        enum restyle restyle = quote_pass(out, arg, size, &current, flags, quote_these_too);
        if (restyle == RESTYLE_NONE) return;
        if (restyle == RESTYLE_C) {
            style = c_quoting_style;
            quote_these_too = 0;
            continue;
        }
        style = current;
        flags &= ~QA_ELIDE_OUTER_QUOTES;
        quote_these_too = 0;
    }
}

char *quotearg_alloc(const char *arg, size_t length, const struct quoting_options *options,
                     size_t *result_length) {
    struct output out = { 0 };
    quote_into(&out, arg, length, options->style, options->flags, options->quote_these_too);
    put(&out, 0);
    if (result_length) *result_length = out.length - 1;
    return out.data;
}

size_t quotearg_buffer(char *buffer, size_t size, const char *arg, size_t length,
                       const struct quoting_options *options) {
    size_t result_length;
    char *quoted = quotearg_alloc(arg, length, options, &result_length);
    if (size) {
        size_t copy = result_length < size ? result_length : size - 1;
        memcpy(buffer, quoted, copy);
        buffer[copy] = 0;
    }
    free(quoted);
    return result_length;
}

#define SLOT_COUNT 8

static char *slots[SLOT_COUNT];

const char *quotearg_n_options(int n, const char *arg, size_t length,
                               const struct quoting_options *options) {
    int slot = n < 0 ? 0 : n % SLOT_COUNT;
    free(slots[slot]);
    slots[slot] = quotearg_alloc(arg, length, options, 0);
    return slots[slot];
}

const char *quotearg_n_style(int n, enum quoting_style style, const char *arg) {
    struct quoting_options options;
    quoting_options_init(&options, style);
    return quotearg_n_options(n, arg, strlen(arg), &options);
}

const char *quotearg_style(enum quoting_style style, const char *arg) {
    return quotearg_n_style(0, style, arg);
}

const char *quotearg_n_style_colon(int n, enum quoting_style style, const char *arg) {
    struct quoting_options options;
    quoting_options_init(&options, style);
    set_char_quoting(&options, ':', true);
    return quotearg_n_options(n, arg, strlen(arg), &options);
}

const char *quote_n(int n, const char *arg) {
    return quotearg_n_style(n + SLOT_COUNT / 2, locale_quoting_style, arg);
}

const char *quote(const char *arg) {
    return quote_n(0, arg);
}

const char *quote_mem(const char *arg, size_t length) {
    struct quoting_options options;
    quoting_options_init(&options, locale_quoting_style);
    return quotearg_n_options(SLOT_COUNT / 2, arg, length, &options);
}
