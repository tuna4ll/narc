#include <caposh/history.h>
#include <caposh/memory.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_SIZE 1000

static char **entries;
static size_t count, capacity, base = 1;

static size_t limit(const char *name) {
    const char *value = var_get(name);
    if (!value || !*value) return DEFAULT_SIZE;
    long parsed = strtol(value, 0, 10);
    return parsed < 0 ? DEFAULT_SIZE : (size_t)parsed;
}

void history_init(void) {
    entries = 0;
    count = capacity = 0;
    base = 1;
}

static void drop_oldest(size_t amount) {
    if (amount > count) amount = count;
    for (size_t i = 0; i < amount; i++) free(entries[i]);
    memmove(entries, entries + amount, (count - amount) * sizeof(char *));
    count -= amount;
    base += amount;
}

static void append(const char *line) {
    size_t maximum = limit("HISTSIZE");
    if (!maximum) return;
    if (count == capacity) {
        capacity = capacity ? capacity * 2 : 64;
        entries = xrealloc(entries, capacity * sizeof(char *));
    }
    entries[count++] = xstrdup(line);
    if (count > maximum) drop_oldest(count - maximum);
}

void history_add(const char *line) {
    const char *control = var_get("HISTCONTROL");
    int ignore_space = control && (strstr(control, "ignorespace") || strstr(control, "ignoreboth"));
    int ignore_dups = control && (strstr(control, "ignoredups") || strstr(control, "ignoreboth"));
    if (ignore_space && line[0] == ' ') return;
    if (ignore_dups && count && !strcmp(entries[count - 1], line)) return;
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n')) length--;
    if (!length) return;
    char *copy = ststrndup(line, length);
    append(copy);
}

size_t history_count(void) {
    return count;
}

size_t history_base(void) {
    return base;
}

const char *history_get(size_t index) {
    return index < count ? entries[index] : 0;
}

void history_clear(void) {
    drop_oldest(count);
}

void history_delete(size_t index) {
    if (index >= count) return;
    free(entries[index]);
    memmove(entries + index, entries + index + 1, (count - index - 1) * sizeof(char *));
    count--;
}

int history_load(const char *path) {
    FILE *file = fopen(path, "re");
    if (!file) return -1;
    char *line = 0;
    size_t size = 0;
    ssize_t length;
    while ((length = getline(&line, &size, file)) > 0) {
        if (line[length - 1] == '\n') line[length - 1] = 0;
        if (*line) append(line);
    }
    free(line);
    fclose(file);
    return 0;
}

int history_save(const char *path) {
    size_t maximum = limit("HISTFILESIZE");
    size_t start = count > maximum ? count - maximum : 0;
    FILE *file = fopen(path, "we");
    if (!file) return -1;
    for (size_t i = start; i < count; i++) fprintf(file, "%s\n", entries[i]);
    return fclose(file);
}

static const char *event_by_prefix(const char *prefix, size_t length, int substring) {
    char *needle = ststrndup(prefix, length);
    for (size_t i = count; i-- > 0;) {
        if (substring ? strstr(entries[i], needle) != 0 : !strncmp(entries[i], needle, length))
            return entries[i];
    }
    return 0;
}

static char **split_words(const char *line, int *total) {
    char **words = stalloc((strlen(line) / 2 + 2) * sizeof(char *));
    int n = 0;
    const char *s = line;
    for (;;) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        const char *start = s;
        char quote = 0;
        while (*s && (quote || (*s != ' ' && *s != '\t'))) {
            if (quote && *s == quote) quote = 0;
            else if (!quote && (*s == '\'' || *s == '"')) quote = *s;
            else if (*s == '\\' && s[1]) s++;
            s++;
        }
        words[n++] = ststrndup(start, (size_t)(s - start));
    }
    words[n] = 0;
    *total = n;
    return words;
}

static const char *select_words(const char *event, const char **cursor, int *ok) {
    const char *s = *cursor;
    *ok = 1;
    int total;
    char **words = split_words(event, &total);
    int first = -1, last = -1;
    if (*s == ':' && s[1] && strchr("0123456789^$*-", s[1])) s++;
    else if (!(*s == '^' || *s == '$' || *s == '*')) return event;
    if (*s == '*') {
        first = 1;
        last = total - 1;
        s++;
        if (total <= 1) {
            *cursor = s;
            return "";
        }
    } else {
        if (*s == '^') first = 1, s++;
        else if (*s == '$') first = total - 1, s++;
        else if (isdigit((unsigned char)*s)) first = (int)strtol(s, (char **)&s, 10);
        else first = 0;
        last = first;
        if (*s == '-') {
            s++;
            if (*s == '$') last = total - 1, s++;
            else if (isdigit((unsigned char)*s)) last = (int)strtol(s, (char **)&s, 10);
            else last = total - 2;
        } else if (*s == '*') {
            last = total - 1;
            s++;
        }
    }
    *cursor = s;
    if (first < 0 || last >= total || first > last) {
        *ok = 0;
        return 0;
    }
    struct strbuf out;
    strbuf_init(&out, stack);
    for (int i = first; i <= last; i++) {
        if (i > first) strbuf_putc(&out, ' ');
        strbuf_puts(&out, words[i]);
    }
    return strbuf_string(&out);
}

char *history_expand(const char *line, int *changed) {
    *changed = 0;
    if (line[0] == '^') {
        const char *old = line + 1, *middle = strchr(old, '^');
        if (!middle || !count) return 0;
        const char *replacement = middle + 1, *end = strchr(replacement, '^');
        size_t old_length = (size_t)(middle - old);
        size_t new_length = end ? (size_t)(end - replacement) : strlen(replacement);
        const char *previous = entries[count - 1];
        const char *found = strstr(previous, ststrndup(old, old_length));
        if (!found || !old_length) {
            fprintf(stderr, "%s: :s%.*s: substitution failed\n", shell.name, (int)old_length, old);
            return 0;
        }
        struct strbuf out;
        strbuf_init(&out, stack);
        strbuf_append(&out, previous, (size_t)(found - previous));
        strbuf_append(&out, replacement, new_length);
        strbuf_puts(&out, found + old_length);
        if (end) strbuf_puts(&out, end + 1);
        *changed = 1;
        return strbuf_string(&out);
    }
    struct strbuf out;
    strbuf_init(&out, stack);
    int single = 0;
    for (const char *s = line; *s;) {
        if (*s == '\'' ) single = !single;
        if (*s == '\\' && s[1]) {
            strbuf_append(&out, s, 2);
            s += 2;
            continue;
        }
        if (single || *s != '!' || !s[1] || strchr(" \t\n=(", s[1])) {
            strbuf_putc(&out, *s++);
            continue;
        }
        const char *cursor = s + 1;
        const char *event = 0;
        if (*cursor == '!') {
            event = count ? entries[count - 1] : 0;
            cursor++;
        } else if (isdigit((unsigned char)*cursor) || *cursor == '-') {
            int negative = *cursor == '-';
            if (negative) cursor++;
            long number = strtol(cursor, (char **)&cursor, 10);
            size_t index = negative ? count - (size_t)number : (size_t)number - base;
            event = (negative ? (size_t)number <= count && number > 0 : (size_t)number >= base)
                        ? history_get(index) : 0;
        } else if (*cursor == '?') {
            const char *end = strchr(cursor + 1, '?');
            size_t length = end ? (size_t)(end - cursor - 1) : strlen(cursor + 1);
            event = event_by_prefix(cursor + 1, length, 1);
            cursor += 1 + length + (end ? 1 : 0);
        } else if (*cursor == '$' || *cursor == '*' || *cursor == '^' || *cursor == ':') {
            event = count ? entries[count - 1] : 0;
        } else {
            const char *end = cursor;
            while (*end && !strchr(" \t\n:;&|<>()\"'", *end)) end++;
            event = event_by_prefix(cursor, (size_t)(end - cursor), 0);
            cursor = end;
        }
        if (!event) {
            fprintf(stderr, "%s: %.*s: event not found\n", shell.name, (int)(cursor - s), s);
            return 0;
        }
        int ok;
        const char *selected = select_words(event, &cursor, &ok);
        if (!ok) {
            fprintf(stderr, "%s: %.*s: bad word specifier\n", shell.name, (int)(cursor - s), s);
            return 0;
        }
        strbuf_puts(&out, selected);
        *changed = 1;
        s = cursor;
    }
    return strbuf_string(&out);
}
