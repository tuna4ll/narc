#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/positional.h>
#include <caposh/shell.h>
#include <caposh/vars.h>
#include <stdlib.h>
#include <string.h>

#define BUCKETS 64
#define NEST_LIMIT 256

static struct function *table[BUCKETS];

static unsigned hash(const char *name) {
    unsigned value = 5381;
    while (*name) value = value * 33 + (unsigned char)*name++;
    return value % BUCKETS;
}

struct function *function_find(const char *name) {
    for (struct function *function = table[hash(name)]; function; function = function->next)
        if (!strcmp(function->name, name)) return function;
    return 0;
}

static void release(struct function *function) {
    if (function->references) {
        function->removed = 1;
        return;
    }
    arena_destroy(&function->arena);
    free(function->name);
    free(function);
}

static void unlink_function(struct function *function) {
    struct function **link = &table[hash(function->name)];
    while (*link && *link != function) link = &(*link)->next;
    if (*link) *link = function->next;
}

void function_define(struct node *definition) {
    struct function *old = function_find(definition->function.name);
    if (old) {
        unlink_function(old);
        release(old);
    }
    struct function *function = xcalloc(1, sizeof(*function));
    function->name = xstrdup(definition->function.name);
    arena_init(&function->arena);
    function->definition = node_copy(&function->arena, definition);
    unsigned bucket = hash(function->name);
    function->next = table[bucket];
    table[bucket] = function;
}

int function_remove(const char *name) {
    struct function *function = function_find(name);
    if (!function) return -1;
    unlink_function(function);
    release(function);
    return 0;
}

int function_call(struct function *function, int argc, char **argv) {
    if (shell.function_depth >= NEST_LIMIT)
        sh_error("%s: maximum function nesting level exceeded (%d)", function->name, NEST_LIMIT);
    char **saved_arguments = shell.arguments;
    int saved_count = shell.argument_count;
    int saved_owned = shell.arguments_owned;
    int saved_loop = shell.loop_depth;
    shell.arguments = argv + 1;
    shell.argument_count = argc - 1;
    shell.arguments_owned = 0;
    shell.loop_depth = 0;
    shell.function_depth++;
    function->references++;
    vars_push_scope();
    volatile int status = 0;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) {
        status = evaluate(function->definition->function.body, 0);
        if (shell.returning) status = shell.status;
    }
    END_TRY(&frame);
    vars_pop_scope();
    shell.returning = 0;
    shell.break_count = shell.continue_count = 0;
    shell.function_depth--;
    shell.loop_depth = saved_loop;
    positional_release();
    shell.arguments = saved_arguments;
    shell.argument_count = saved_count;
    shell.arguments_owned = saved_owned;
    if (!--function->references && function->removed) release(function);
    if (kind) raise_exception(kind);
    shell.status = status;
    return status;
}

static int compare(const void *a, const void *b) {
    return strcmp((*(struct function *const *)a)->name, (*(struct function *const *)b)->name);
}

struct function **functions_sorted(int *count) {
    int total = 0;
    for (int i = 0; i < BUCKETS; i++)
        for (struct function *f = table[i]; f; f = f->next) total++;
    struct function **list = stalloc(((size_t)total + 1) * sizeof(*list));
    int index = 0;
    for (int i = 0; i < BUCKETS; i++)
        for (struct function *f = table[i]; f; f = f->next) list[index++] = f;
    qsort(list, (size_t)total, sizeof(*list), compare);
    list[total] = 0;
    *count = total;
    return list;
}
