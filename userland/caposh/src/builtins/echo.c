#include <caposh/builtins.h>
#include <caposh/strbuf.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int echo_escapes(struct strbuf *out, const char *text, int octal_zero);

int echo_escapes(struct strbuf *out, const char *text, int octal_zero) {
    for (const char *s = text; *s; s++) {
        if (*s != '\\' || !s[1]) {
            strbuf_putc(out, *s);
            continue;
        }
        char c = *++s;
        switch (c) {
        case 'a': strbuf_putc(out, '\a'); break;
        case 'b': strbuf_putc(out, '\b'); break;
        case 'c': return 1;
        case 'e': case 'E': strbuf_putc(out, 0x1b); break;
        case 'f': strbuf_putc(out, '\f'); break;
        case 'n': strbuf_putc(out, '\n'); break;
        case 'r': strbuf_putc(out, '\r'); break;
        case 't': strbuf_putc(out, '\t'); break;
        case 'v': strbuf_putc(out, '\v'); break;
        case '\\': strbuf_putc(out, '\\'); break;
        case 'x': {
            int value = 0, digits = 0;
            while (digits < 2 && isxdigit((unsigned char)s[1])) {
                char h = *++s;
                value = value * 16 + (isdigit((unsigned char)h) ? h - '0' : tolower(h) - 'a' + 10);
                digits++;
            }
            if (digits) strbuf_putc(out, (char)value);
            else strbuf_append(out, "\\x", 2);
            break;
        }
        default:
            if ((octal_zero && c == '0') || (!octal_zero && c >= '0' && c <= '7')) {
                int value = octal_zero ? 0 : c - '0', digits = octal_zero ? 0 : 1;
                while (digits < 3 && s[1] >= '0' && s[1] <= '7') {
                    value = value * 8 + (*++s - '0');
                    digits++;
                }
                strbuf_putc(out, (char)value);
            } else {
                strbuf_putc(out, '\\');
                strbuf_putc(out, c);
            }
        }
    }
    return 0;
}

int builtin_echo(int argc, char **argv) {
    int newline = 1, escapes = 0, index = 1;
    for (; index < argc; index++) {
        const char *argument = argv[index];
        if (argument[0] != '-' || !argument[1] || strspn(argument + 1, "neE") != strlen(argument + 1))
            break;
        for (const char *flag = argument + 1; *flag; flag++) {
            if (*flag == 'n') newline = 0;
            else if (*flag == 'e') escapes = 1;
            else escapes = 0;
        }
    }
    struct strbuf out;
    strbuf_init(&out, stack);
    int stop = 0;
    for (int i = index; i < argc && !stop; i++) {
        if (i > index) strbuf_putc(&out, ' ');
        if (escapes) stop = echo_escapes(&out, argv[i], 1);
        else strbuf_puts(&out, argv[i]);
    }
    if (newline && !stop) strbuf_putc(&out, '\n');
    if (out.length && fwrite(out.data, 1, out.length, stdout) != out.length) return 1;
    return fflush(stdout) ? 1 : 0;
}
