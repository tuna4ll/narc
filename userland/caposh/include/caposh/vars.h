#pragma once

#include <caposh/memory.h>
#include <stddef.h>

#define VAR_EXPORT   0x01
#define VAR_READONLY 0x02
#define VAR_INTEGER  0x04
#define VAR_UNSET    0x08
#define VAR_LOWER    0x10
#define VAR_UPPER    0x20

struct var {
    struct var *next;
    char *name;
    char *value;
    int flags;
};

void vars_init(char **environment);
const char *var_get(const char *name);
struct var *var_lookup(const char *name);
int var_set(const char *name, const char *value, int flags);
int var_set_integer(const char *name, long long value);
int var_unset(const char *name);
int var_add_flags(const char *name, int flags);
int var_remove_flags(const char *name, int flags);
int var_assign(const char *assignment, int flags);

void vars_push_scope(void);
void vars_pop_scope(void);
int vars_scope_depth(void);
int var_make_local(const char *name);

char **vars_environment(struct arena *arena);
struct var **vars_sorted(size_t *count);
void var_print_assignment(const struct var *var, const char *prefix);
char *shell_quote(struct arena *arena, const char *value);
