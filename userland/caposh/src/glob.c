#include <caposh/glob.h>
#include <caposh/memory.h>
#include <caposh/strbuf.h>
#include <dirent.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct results {
    char **items;
    int count, capacity;
};

int glob_has_magic(const char *pattern) {
    for (const char *s = pattern; *s; s++) {
        if (*s == '\\' && s[1]) s++;
        else if (*s == '*' || *s == '?' || *s == '[') return 1;
    }
    return 0;
}

char *glob_unescape(const char *pattern) {
    struct strbuf out;
    strbuf_init(&out, stack);
    for (const char *s = pattern; *s; s++) {
        if (*s == '\\' && s[1]) s++;
        strbuf_putc(&out, *s);
    }
    return strbuf_string(&out);
}

static void add(struct results *results, const char *path) {
    if (results->count == results->capacity) {
        int capacity = results->capacity ? results->capacity * 2 : 16;
        char **items = stalloc((size_t)capacity * sizeof(char *));
        if (results->count) memcpy(items, results->items, (size_t)results->count * sizeof(char *));
        results->items = items;
        results->capacity = capacity;
    }
    results->items[results->count++] = ststrdup(path);
}

static int is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static void expand(struct results *results, const char *prefix, const char *rest) {
    while (*rest == '/') rest++;
    if (!*rest) {
        add(results, prefix);
        return;
    }
    const char *slash = rest;
    while (*slash && *slash != '/') slash += *slash == '\\' && slash[1] ? 2 : 1;
    char *component = ststrndup(rest, (size_t)(slash - rest));
    int trailing = *slash == '/';
    size_t prefix_length = strlen(prefix);
    int needs_slash = prefix_length && prefix[prefix_length - 1] != '/';
    if (!glob_has_magic(component)) {
        struct strbuf path;
        strbuf_init(&path, stack);
        strbuf_puts(&path, prefix);
        if (needs_slash) strbuf_putc(&path, '/');
        strbuf_puts(&path, glob_unescape(component));
        char *next = strbuf_string(&path);
        if (trailing) {
            if (is_directory(next)) expand(results, next, slash);
        } else {
            struct stat info;
            if (stat(next, &info) == 0) add(results, next);
        }
        return;
    }
    DIR *dir = opendir(prefix_length ? prefix : ".");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (fnmatch(component, name, FNM_PERIOD)) continue;
        struct strbuf path;
        strbuf_init(&path, stack);
        strbuf_puts(&path, prefix);
        if (needs_slash) strbuf_putc(&path, '/');
        strbuf_puts(&path, name);
        char *next = strbuf_string(&path);
        if (trailing) {
            if (is_directory(next)) expand(results, next, slash);
        } else {
            add(results, next);
        }
    }
    closedir(dir);
}

static int compare(const void *a, const void *b) {
    return strcoll(*(char *const *)a, *(char *const *)b);
}

int glob_expand(const char *pattern, char ***matches) {
    struct results results = { 0, 0, 0 };
    int absolute = pattern[0] == '/';
    expand(&results, absolute ? "/" : "", pattern + absolute);
    if (pattern[strlen(pattern) - 1] == '/') {
        for (int i = 0; i < results.count; i++) {
            size_t length = strlen(results.items[i]);
            if (!length || results.items[i][length - 1] != '/') {
                char *with = stalloc(length + 2);
                memcpy(with, results.items[i], length);
                with[length] = '/';
                with[length + 1] = 0;
                results.items[i] = with;
            }
        }
    }
    qsort(results.items, (size_t)results.count, sizeof(char *), compare);
    *matches = results.items;
    return results.count;
}
