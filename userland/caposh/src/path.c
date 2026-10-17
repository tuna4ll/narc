#include <caposh/memory.h>
#include <caposh/options.h>
#include <caposh/path.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct hashed {
    struct hashed *next;
    char *name;
    char *path;
    int hits;
};

static struct hashed *hashed;

int path_executable(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && (info.st_mode & 0111);
}

static struct hashed *find(const char *name) {
    for (struct hashed *entry = hashed; entry; entry = entry->next)
        if (!strcmp(entry->name, name)) return entry;
    return 0;
}

static char *search(const char *name) {
    const char *path = var_get("PATH");
    if (!path) path = "";
    for (const char *cursor = path;;) {
        const char *end = strchr(cursor, ':');
        if (!end) end = cursor + strlen(cursor);
        struct strbuf candidate;
        strbuf_init(&candidate, stack);
        if (end == cursor) strbuf_putc(&candidate, '.');
        else strbuf_append(&candidate, cursor, (size_t)(end - cursor));
        strbuf_putc(&candidate, '/');
        strbuf_puts(&candidate, name);
        char *full = strbuf_string(&candidate);
        if (path_executable(full)) return full;
        if (!*end) return 0;
        cursor = end + 1;
    }
}

char *path_search(const char *name, int use_hash) {
    if (use_hash && option_enabled(OPTION_HASHALL)) {
        struct hashed *entry = find(name);
        if (entry && path_executable(entry->path)) {
            entry->hits++;
            return ststrdup(entry->path);
        }
    }
    char *found = search(name);
    if (found && use_hash && option_enabled(OPTION_HASHALL)) {
        struct hashed *entry = find(name);
        if (!entry) {
            entry = xcalloc(1, sizeof(*entry));
            entry->name = xstrdup(name);
            entry->next = hashed;
            hashed = entry;
        } else {
            free(entry->path);
        }
        entry->path = xstrdup(found);
        entry->hits = 1;
    }
    return found;
}

void path_hash_remove(const char *name) {
    for (struct hashed **link = &hashed; *link; link = &(*link)->next) {
        struct hashed *entry = *link;
        if (strcmp(entry->name, name)) continue;
        *link = entry->next;
        free(entry->name);
        free(entry->path);
        free(entry);
        return;
    }
}

void path_hash_clear(void) {
    while (hashed) path_hash_remove(hashed->name);
}

int path_hash_add(const char *name) {
    path_hash_remove(name);
    return path_search(name, 1) ? 0 : -1;
}

void path_hash_print(void) {
    if (!hashed) {
        printf("hash: hash table empty\n");
        return;
    }
    printf("hits\tcommand\n");
    for (struct hashed *entry = hashed; entry; entry = entry->next)
        printf("%4d\t%s\n", entry->hits, entry->path);
}
