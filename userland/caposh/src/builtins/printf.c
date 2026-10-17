#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int echo_escapes(struct strbuf *out, const char *text, int octal_zero);

struct arguments {
    char **items;
    int count, index;
    int status;
};

static const char *next_argument(struct arguments *args) {
    return args->index < args->count ? args->items[args->index++] : 0;
}

static long long integer_argument(struct arguments *args) {
    const char *text = next_argument(args);
    if (!text || !*text) return 0;
    if ((text[0] == '\'' || text[0] == '"') && text[1]) return (unsigned char)text[1];
    char *end;
    errno = 0;
    long long value = strtoll(text, &end, 0);
    if (*end || errno) {
        builtin_error("printf", "%s: invalid number", text);
        args->status = 1;
    }
    return value;
}

static double float_argument(struct arguments *args) {
    const char *text = next_argument(args);
    if (!text || !*text) return 0;
    if ((text[0] == '\'' || text[0] == '"') && text[1]) return (unsigned char)text[1];
    char *end;
    double value = (double)strtoll(text, &end, 10);
    if (*end == '.') {
        double scale = 0.1;
        for (end++; isdigit((unsigned char)*end); end++, scale /= 10) {
            double digit = (*end - '0') * scale;
            value = value < 0 || text[0] == '-' ? value - digit : value + digit;
        }
    }
    if (*end == 'e' || *end == 'E') {
        long exponent = strtol(end + 1, &end, 10);
        for (; exponent > 0; exponent--) value *= 10;
        for (; exponent < 0; exponent++) value /= 10;
    }
    if (*end) {
        builtin_error("printf", "%s: invalid number", text);
        args->status = 1;
    }
    return value;
}

static char *backslash_quote(const char *text) {
    struct strbuf out;
    strbuf_init(&out, stack);
    if (!*text) return "''";
    int control = 0;
    for (const char *c = text; *c; c++) control |= (unsigned char)*c < 32 || *c == 0x7f;
    if (control) {
        strbuf_append(&out, "$'", 2);
        for (const char *c = text; *c; c++) {
            switch (*c) {
            case '\n': strbuf_append(&out, "\\n", 2); break;
            case '\t': strbuf_append(&out, "\\t", 2); break;
            case '\r': strbuf_append(&out, "\\r", 2); break;
            case 0x1b: strbuf_append(&out, "\\E", 2); break;
            case '\'': case '\\': strbuf_putc(&out, '\\'); strbuf_putc(&out, *c); break;
            default:
                if ((unsigned char)*c < 32 || *c == 0x7f) strbuf_printf(&out, "\\%03o", (unsigned char)*c);
                else strbuf_putc(&out, *c);
            }
        }
        strbuf_putc(&out, '\'');
        return strbuf_string(&out);
    }
    for (const char *c = text; *c; c++) {
        if (!isalnum((unsigned char)*c) && !strchr("_-./,:=+@%^", *c) && !((unsigned char)*c & 0x80))
            strbuf_putc(&out, '\\');
        strbuf_putc(&out, *c);
    }
    return strbuf_string(&out);
}

static int format_once(struct strbuf *out, const char *format, struct arguments *args) {
    for (const char *s = format; *s; s++) {
        if (*s == '\\') {
            size_t length = 1;
            if (s[1] == 'x') {
                length = 2;
                while (length < 4 && isxdigit((unsigned char)s[length])) length++;
            } else if (s[1] >= '0' && s[1] <= '7') {
                length = 1;
                while (length < 4 && s[length] >= '0' && s[length] <= '7') length++;
            } else if (s[1]) {
                length = 2;
            }
            char *piece = ststrndup(s, length);
            if (echo_escapes(out, piece, 0)) return 1;
            s += length - 1;
            continue;
        }
        if (*s != '%') {
            strbuf_putc(out, *s);
            continue;
        }
        if (s[1] == '%') {
            strbuf_putc(out, '%');
            s++;
            continue;
        }
        char spec[64];
        size_t n = 0;
        spec[n++] = '%';
        s++;
        while (*s && strchr("-+ #0", *s) && n < 20) spec[n++] = *s++;
        if (*s == '*') {
            n += (size_t)snprintf(spec + n, sizeof(spec) - n, "%d", (int)integer_argument(args));
            s++;
        } else {
            while (isdigit((unsigned char)*s) && n < 40) spec[n++] = *s++;
        }
        if (*s == '.') {
            spec[n++] = *s++;
            if (*s == '*') {
                n += (size_t)snprintf(spec + n, sizeof(spec) - n, "%d", (int)integer_argument(args));
                s++;
            } else {
                while (isdigit((unsigned char)*s) && n < 55) spec[n++] = *s++;
            }
        }
        while (*s && strchr("hlLjzt", *s)) s++;
        char conversion = *s;
        if (!conversion) {
            builtin_error("printf", "%s: missing format character", format);
            args->status = 1;
            return 1;
        }
        switch (conversion) {
        case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'c': {
            if (conversion == 'c') {
                const char *text = next_argument(args);
                spec[n++] = 'c';
                spec[n] = 0;
                strbuf_printf(out, spec, text && *text ? text[0] : 0);
                break;
            }
            spec[n++] = 'l';
            spec[n++] = 'l';
            spec[n++] = conversion;
            spec[n] = 0;
            strbuf_printf(out, spec, integer_argument(args));
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
            spec[n++] = conversion;
            spec[n] = 0;
            strbuf_printf(out, spec, float_argument(args));
            break;
        case 's': {
            const char *text = next_argument(args);
            spec[n++] = 's';
            spec[n] = 0;
            strbuf_printf(out, spec, text ? text : "");
            break;
        }
        case 'b': {
            const char *text = next_argument(args);
            struct strbuf expanded;
            strbuf_init(&expanded, stack);
            int stop = echo_escapes(&expanded, text ? text : "", 1);
            spec[n++] = 's';
            spec[n] = 0;
            strbuf_printf(out, spec, strbuf_string(&expanded));
            if (stop) return 1;
            break;
        }
        case 'q': {
            const char *text = next_argument(args);
            spec[n++] = 's';
            spec[n] = 0;
            strbuf_printf(out, spec, backslash_quote(text ? text : ""));
            break;
        }
        default:
            builtin_error("printf", "%%%c: invalid format character", conversion);
            args->status = 1;
            return 1;
        }
    }
    return 0;
}

int builtin_printf(int argc, char **argv) {
    const char *variable = 0;
    int index = 1;
    if (index + 1 < argc && !strcmp(argv[index], "-v")) {
        variable = argv[index + 1];
        index += 2;
    }
    if (index < argc && !strcmp(argv[index], "--")) index++;
    if (index >= argc) return builtin_usage("printf");
    const char *format = argv[index++];
    struct arguments args = { argv + index, argc - index, 0, 0 };
    struct strbuf out;
    strbuf_init(&out, stack);
    for (;;) {
        int start = args.index;
        if (format_once(&out, format, &args)) break;
        if (args.index >= args.count || args.index == start) break;
    }
    if (variable) {
        if (var_set(variable, strbuf_string(&out), 0) != 0) return 1;
    } else if (out.length && fwrite(out.data, 1, out.length, stdout) != out.length) {
        return 1;
    }
    return args.status;
}
