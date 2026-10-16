#pragma once

struct alias {
    struct alias *next;
    char *name;
    char *value;
    int active;
};

struct alias *alias_find(const char *name);
void alias_set(const char *name, const char *value);
int alias_remove(const char *name);
void alias_clear(void);
void alias_print(const struct alias *alias);
void alias_print_all(void);
