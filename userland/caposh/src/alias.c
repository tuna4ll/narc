#include <caposh/alias.h>
#include <caposh/memory.h>
#include <caposh/vars.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct alias *aliases;

struct alias *alias_find(const char *name) {
    for (struct alias *alias = aliases; alias; alias = alias->next)
        if (!strcmp(alias->name, name)) return alias;
    return 0;
}

void alias_set(const char *name, const char *value) {
    struct alias *alias = alias_find(name);
    if (alias) {
        char *copy = xstrdup(value);
        free(alias->value);
        alias->value = copy;
        return;
    }
    alias = xcalloc(1, sizeof(*alias));
    alias->name = xstrdup(name);
    alias->value = xstrdup(value);
    struct alias **link = &aliases;
    while (*link && strcmp((*link)->name, name) < 0) link = &(*link)->next;
    alias->next = *link;
    *link = alias;
}

int alias_remove(const char *name) {
    for (struct alias **link = &aliases; *link; link = &(*link)->next) {
        struct alias *alias = *link;
        if (strcmp(alias->name, name)) continue;
        if (alias->active) {
            free(alias->value);
            alias->value = xstrdup("");
            return 0;
        }
        *link = alias->next;
        free(alias->name);
        free(alias->value);
        free(alias);
        return 0;
    }
    return -1;
}

void alias_clear(void) {
    struct alias **link = &aliases;
    while (*link) {
        struct alias *alias = *link;
        if (alias->active) {
            free(alias->value);
            alias->value = xstrdup("");
            link = &alias->next;
            continue;
        }
        *link = alias->next;
        free(alias->name);
        free(alias->value);
        free(alias);
    }
}

void alias_print(const struct alias *alias) {
    struct arena_mark mark = arena_mark(stack);
    printf("alias %s=%s\n", alias->name, shell_quote(stack, alias->value));
    arena_release(stack, mark);
}

void alias_print_all(void) {
    for (struct alias *alias = aliases; alias; alias = alias->next) alias_print(alias);
}
