#include <caposh/error.h>
#include <caposh/expand.h>
#include <caposh/history.h>
#include <caposh/jobs.h>
#include <caposh/memory.h>
#include <caposh/prompt.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define IGNORE_START '\001'
#define IGNORE_END '\002'

static int command_number = 1;

static void quoted(struct strbuf *out, const char *value) {
    for (; *value; value++) {
        if (strchr("$`\\\"", *value)) strbuf_putc(out, '\\');
        strbuf_putc(out, *value);
    }
}

static void host(struct strbuf *out, int full) {
    struct utsname info;
    if (uname(&info) != 0) return;
    size_t length = full ? strlen(info.nodename) : strcspn(info.nodename, ".");
    quoted(out, ststrndup(info.nodename, length));
}

static void user(struct strbuf *out) {
    const char *name = var_get("USER");
    if (!name) {
        struct passwd *entry = getpwuid(geteuid());
        name = entry ? entry->pw_name : "root";
    }
    quoted(out, name);
}

static void directory(struct strbuf *out, int base_only) {
    const char *pwd = var_get("PWD");
    char *cwd = 0;
    if (!pwd) pwd = cwd = getcwd(0, 0);
    if (!pwd) return;
    const char *home = var_get("HOME");
    if (base_only) {
        const char *slash = strrchr(pwd, '/');
        if (home && !strcmp(pwd, home)) quoted(out, "~");
        else quoted(out, slash && slash[1] ? slash + 1 : pwd);
    } else if (home && *home && strcmp(home, "/") && !strncmp(pwd, home, strlen(home)) &&
               (pwd[strlen(home)] == '/' || !pwd[strlen(home)])) {
        strbuf_putc(out, '~');
        quoted(out, pwd + strlen(home));
    } else {
        quoted(out, pwd);
    }
    free(cwd);
}

static void clock_text(struct strbuf *out, const char *format) {
    time_t now = time(0);
    struct tm tm;
    localtime_r(&now, &tm);
    char buffer[128];
    if (strftime(buffer, sizeof(buffer), format, &tm)) quoted(out, buffer);
}

static int count_jobs(void) {
    int n = 0;
    for (struct job *job = jobs_list(); job; job = job->next) n++;
    return n;
}

static char *decode(const char *text) {
    struct strbuf out;
    strbuf_init(&out, stack);
    for (const char *s = text; *s; s++) {
        if (*s != '\\' || !s[1]) {
            strbuf_putc(&out, *s);
            continue;
        }
        char c = *++s;
        switch (c) {
        case 'a': strbuf_putc(&out, '\a'); break;
        case 'e': strbuf_putc(&out, 0x1b); break;
        case 'n': strbuf_putc(&out, '\n'); break;
        case 'r': strbuf_putc(&out, '\r'); break;
        case 's': quoted(&out, CAPOSH_NAME); break;
        case 'v': quoted(&out, "1.0"); break;
        case 'V': quoted(&out, CAPOSH_VERSION); break;
        case 'h': host(&out, 0); break;
        case 'H': host(&out, 1); break;
        case 'u': user(&out); break;
        case 'w': directory(&out, 0); break;
        case 'W': directory(&out, 1); break;
        case 'd': clock_text(&out, "%a %b %d"); break;
        case 't': clock_text(&out, "%H:%M:%S"); break;
        case 'T': clock_text(&out, "%I:%M:%S"); break;
        case '@': clock_text(&out, "%I:%M %p"); break;
        case 'A': clock_text(&out, "%H:%M"); break;
        case 'D': {
            if (s[1] != '{') {
                strbuf_append(&out, "\\D", 2);
                break;
            }
            const char *end = strchr(s + 2, '}');
            if (!end) {
                strbuf_append(&out, "\\D", 2);
                break;
            }
            char *format = ststrndup(s + 2, (size_t)(end - s - 2));
            clock_text(&out, *format ? format : "%X");
            s = end;
            break;
        }
        case 'j': strbuf_printf(&out, "%d", count_jobs()); break;
        case 'l': quoted(&out, "console"); break;
        case '!': strbuf_printf(&out, "%zu", history_base() + history_count()); break;
        case '#': strbuf_printf(&out, "%d", command_number); break;
        case '$': strbuf_puts(&out, geteuid() == 0 ? "#" : "\\$"); break;
        case '[': strbuf_putc(&out, IGNORE_START); break;
        case ']': strbuf_putc(&out, IGNORE_END); break;
        case '\\': strbuf_append(&out, "\\\\", 2); break;
        default:
            if (c >= '0' && c <= '7') {
                int value = c - '0', digits = 1;
                while (digits < 3 && s[1] >= '0' && s[1] <= '7') {
                    value = value * 8 + (*++s - '0');
                    digits++;
                }
                strbuf_putc(&out, (char)value);
            } else {
                strbuf_putc(&out, '\\');
                strbuf_putc(&out, c);
            }
        }
    }
    return strbuf_string(&out);
}

char *prompt_render(int level) {
    const char *source = var_get(level == 1 ? "PS1" : "PS2");
    if (!source) source = level == 1 ? (geteuid() == 0 ? "\\s-\\v# " : "\\s-\\v\\$ ") : "> ";
    if (!shell.interactive) return xstrdup("");
    struct arena_mark mark = arena_mark(stack);
    char *decoded = decode(source);
    char *volatile result = 0;
    int saved_status = shell.status;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) result = xstrdup(expand_prompt_parameters(decoded));
    END_TRY(&frame);
    if (kind) result = xstrdup(decoded);
    shell.status = saved_status;
    arena_release(stack, mark);
    if (level == 1) command_number++;
    return result;
}

size_t prompt_visible_width(const char *prompt) {
    size_t width = 0;
    int hidden = 0;
    for (const char *s = prompt; *s; s++) {
        if (*s == IGNORE_START) hidden = 1;
        else if (*s == IGNORE_END) hidden = 0;
        else if (*s == '\n') width = 0;
        else if (!hidden && ((unsigned char)*s & 0xc0) != 0x80) width++;
    }
    return width;
}
