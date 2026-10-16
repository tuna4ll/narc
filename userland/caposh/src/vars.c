#include <caposh/arith.h>
#include <caposh/error.h>
#include <caposh/input.h>
#include <caposh/options.h>
#include <caposh/parser.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define BUCKETS 256

struct saved {
    struct saved *next;
    char *name;
    char *value;
    int flags;
    int existed;
};

struct scope {
    struct scope *previous;
    struct saved *saved;
};

static struct var *table[BUCKETS];
static struct scope *scopes;
static int scope_depth;
static unsigned long long random_state;
static char dynamic_value[32];

static unsigned hash(const char *name) {
    unsigned value = 5381;
    while (*name) value = value * 33 + (unsigned char)*name++;
    return value % BUCKETS;
}

struct var *var_lookup(const char *name) {
    for (struct var *var = table[hash(name)]; var; var = var->next)
        if (!strcmp(var->name, name)) return var;
    return 0;
}

static struct var *create(const char *name) {
    struct var *var = xcalloc(1, sizeof(*var));
    var->name = xstrdup(name);
    var->flags = VAR_UNSET;
    unsigned bucket = hash(name);
    var->next = table[bucket];
    table[bucket] = var;
    return var;
}

static int next_random(void) {
    random_state = random_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((random_state >> 33) % 32768);
}

static int64_t now_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec;
}

const char *var_get(const char *name) {
    if (!strcmp(name, "RANDOM") && !(var_lookup(name) && (var_lookup(name)->flags & VAR_UNSET))) {
        snprintf(dynamic_value, sizeof(dynamic_value), "%d", next_random());
        return dynamic_value;
    }
    if (!strcmp(name, "SECONDS")) {
        snprintf(dynamic_value, sizeof(dynamic_value), "%lld",
                 (long long)(now_seconds() - shell.started));
        return dynamic_value;
    }
    if (!strcmp(name, "LINENO")) {
        snprintf(dynamic_value, sizeof(dynamic_value), "%d", shell.line);
        return dynamic_value;
    }
    struct var *var = var_lookup(name);
    return var && !(var->flags & VAR_UNSET) ? var->value : 0;
}

static char *transform(struct var *var, const char *value) {
    if (var->flags & VAR_INTEGER) {
        long long number = arith_evaluate(value);
        char text[32];
        snprintf(text, sizeof(text), "%lld", number);
        return xstrdup(text);
    }
    char *copy = xstrdup(value);
    if (var->flags & (VAR_LOWER | VAR_UPPER))
        for (char *c = copy; *c; c++)
            *c = (char)((var->flags & VAR_UPPER) ? toupper((unsigned char)*c) :
                                                   tolower((unsigned char)*c));
    return copy;
}

int var_set(const char *name, const char *value, int flags) {
    if (!strcmp(name, "RANDOM") && value) {
        random_state = strtoull(value, 0, 10);
        return 0;
    }
    if (!strcmp(name, "SECONDS") && value) {
        shell.started = now_seconds() - strtoll(value, 0, 10);
        return 0;
    }
    struct var *var = var_lookup(name);
    if (var && (var->flags & VAR_READONLY) && value) {
        sh_warning("%s: readonly variable", name);
        return -1;
    }
    if (!var) var = create(name);
    var->flags |= flags & ~VAR_UNSET;
    if (option_enabled(OPTION_ALLEXPORT)) var->flags |= VAR_EXPORT;
    if (value) {
        char *next = transform(var, value);
        free(var->value);
        var->value = next;
        var->flags &= ~VAR_UNSET;
    }
    return 0;
}

int var_set_integer(const char *name, long long value) {
    char text[32];
    snprintf(text, sizeof(text), "%lld", value);
    return var_set(name, text, 0);
}

int var_unset(const char *name) {
    struct var *var = var_lookup(name);
    if (!var) return 0;
    if (var->flags & VAR_READONLY) {
        sh_warning("unset: %s: cannot unset: readonly variable", name);
        return -1;
    }
    if (scope_depth) {
        free(var->value);
        var->value = 0;
        var->flags = VAR_UNSET;
        return 0;
    }
    struct var **link = &table[hash(name)];
    while (*link != var) link = &(*link)->next;
    *link = var->next;
    free(var->name);
    free(var->value);
    free(var);
    return 0;
}

int var_add_flags(const char *name, int flags) {
    struct var *var = var_lookup(name);
    if (!var) var = create(name);
    var->flags |= flags;
    if ((flags & VAR_INTEGER) && var->value) {
        char *next = transform(var, var->value);
        free(var->value);
        var->value = next;
    }
    return 0;
}

int var_remove_flags(const char *name, int flags) {
    struct var *var = var_lookup(name);
    if (var) var->flags &= ~flags;
    return 0;
}

int var_assign(const char *assignment, int flags) {
    size_t length = assignment_name_length(assignment);
    if (!length) return -1;
    char *name = ststrndup(assignment, length);
    const char *value = assignment + length + 1;
    if (assignment[length] == '+') {
        value++;
        const char *old = var_get(name);
        struct var *var = var_lookup(name);
        if (var && (var->flags & VAR_INTEGER)) {
            long long sum = (old ? arith_evaluate(old) : 0) + arith_evaluate(value);
            char text[32];
            snprintf(text, sizeof(text), "%lld", sum);
            return var_set(name, text, flags);
        }
        struct strbuf joined;
        strbuf_init(&joined, stack);
        if (old) strbuf_puts(&joined, old);
        strbuf_puts(&joined, value);
        return var_set(name, strbuf_string(&joined), flags);
    }
    return var_set(name, value, flags);
}

void vars_push_scope(void) {
    struct scope *scope = xcalloc(1, sizeof(*scope));
    scope->previous = scopes;
    scopes = scope;
    scope_depth++;
}

void vars_pop_scope(void) {
    struct scope *scope = scopes;
    if (!scope) return;
    while (scope->saved) {
        struct saved *saved = scope->saved;
        scope->saved = saved->next;
        struct var *var = var_lookup(saved->name);
        if (saved->existed) {
            if (!var) var = create(saved->name);
            free(var->value);
            var->value = saved->value;
            var->flags = saved->flags;
        } else if (var) {
            struct var **link = &table[hash(saved->name)];
            while (*link != var) link = &(*link)->next;
            *link = var->next;
            free(var->name);
            free(var->value);
            free(var);
        }
        free(saved->name);
        free(saved);
    }
    scopes = scope->previous;
    scope_depth--;
    free(scope);
}

int vars_scope_depth(void) {
    return scope_depth;
}

int var_make_local(const char *name) {
    if (!scopes) return -1;
    for (struct saved *saved = scopes->saved; saved; saved = saved->next)
        if (!strcmp(saved->name, name)) return 0;
    struct var *var = var_lookup(name);
    if (var && (var->flags & VAR_READONLY)) {
        sh_warning("local: %s: readonly variable", name);
        return -1;
    }
    struct saved *saved = xcalloc(1, sizeof(*saved));
    saved->name = xstrdup(name);
    saved->existed = var != 0;
    if (var) {
        saved->value = var->value ? xstrdup(var->value) : 0;
        saved->flags = var->flags;
    }
    saved->next = scopes->saved;
    scopes->saved = saved;
    if (!var) var = create(name);
    var->flags = (var->flags & VAR_EXPORT) | VAR_UNSET;
    free(var->value);
    var->value = 0;
    return 0;
}

char **vars_environment(struct arena *arena) {
    size_t count = 0;
    for (int i = 0; i < BUCKETS; i++)
        for (struct var *var = table[i]; var; var = var->next)
            if ((var->flags & VAR_EXPORT) && !(var->flags & VAR_UNSET)) count++;
    char **environment = arena_alloc(arena, (count + 1) * sizeof(char *));
    size_t index = 0;
    for (int i = 0; i < BUCKETS; i++) {
        for (struct var *var = table[i]; var; var = var->next) {
            if (!(var->flags & VAR_EXPORT) || (var->flags & VAR_UNSET)) continue;
            size_t name = strlen(var->name), value = strlen(var->value);
            char *entry = arena_alloc(arena, name + value + 2);
            memcpy(entry, var->name, name);
            entry[name] = '=';
            memcpy(entry + name + 1, var->value, value + 1);
            environment[index++] = entry;
        }
    }
    environment[index] = 0;
    return environment;
}

static int compare(const void *a, const void *b) {
    return strcmp((*(struct var *const *)a)->name, (*(struct var *const *)b)->name);
}

struct var **vars_sorted(size_t *count) {
    size_t total = 0;
    for (int i = 0; i < BUCKETS; i++)
        for (struct var *var = table[i]; var; var = var->next) total++;
    struct var **list = stalloc((total + 1) * sizeof(*list));
    size_t index = 0;
    for (int i = 0; i < BUCKETS; i++)
        for (struct var *var = table[i]; var; var = var->next) list[index++] = var;
    qsort(list, total, sizeof(*list), compare);
    list[total] = 0;
    *count = total;
    return list;
}

char *shell_quote(struct arena *arena, const char *value) {
    int plain = *value != 0;
    for (const char *c = value; *c && plain; c++)
        plain = isalnum((unsigned char)*c) || strchr("_-+./:,@%=", *c);
    if (plain) return arena_strdup(arena, value);
    struct strbuf buffer;
    strbuf_init(&buffer, arena);
    strbuf_putc(&buffer, '\'');
    for (const char *c = value; *c; c++) {
        if (*c == '\'') strbuf_puts(&buffer, "'\\''");
        else strbuf_putc(&buffer, *c);
    }
    strbuf_putc(&buffer, '\'');
    return strbuf_string(&buffer);
}

void var_print_assignment(const struct var *var, const char *prefix) {
    if (prefix) fputs(prefix, stdout);
    fputs(var->name, stdout);
    if (!(var->flags & VAR_UNSET)) {
        struct arena_mark mark = arena_mark(stack);
        printf("=%s", shell_quote(stack, var->value));
        arena_release(stack, mark);
    }
    fputc('\n', stdout);
}

static void import(char **environment) {
    for (char **entry = environment; entry && *entry; entry++) {
        size_t length = strcspn(*entry, "=");
        if (!(*entry)[length] || !is_name_prefix(*entry, length)) continue;
        char *name = xstrndup(*entry, length);
        var_set(name, *entry + length + 1, VAR_EXPORT);
        free(name);
    }
}

static void default_value(const char *name, const char *value, int flags) {
    if (!var_get(name)) var_set(name, value, flags);
}

void vars_init(char **environment) {
    random_state = (unsigned long long)getpid() * 2654435761ULL ^ (unsigned long long)time(0);
    import(environment);
    default_value("PATH", "/bin:/sbin:/usr/bin", VAR_EXPORT);
    default_value("IFS", " \t\n", 0);
    default_value("PS2", "> ", 0);
    default_value("PS4", "+ ", 0);
    default_value("OPTIND", "1", 0);
    default_value("HOME", "/", VAR_EXPORT);
    var_set("CAPOSH", "/bin/caposh", 0);
    var_set("CAPOSH_VERSION", CAPOSH_VERSION, 0);
    var_set_integer("PPID", getppid());
    const char *level = var_get("SHLVL");
    var_set_integer("SHLVL", level ? atoll(level) + 1 : 1);
    var_add_flags("SHLVL", VAR_EXPORT);
    struct utsname info;
    if (uname(&info) == 0) {
        default_value("HOSTNAME", info.nodename, 0);
        default_value("HOSTTYPE", info.machine, 0);
        default_value("OSTYPE", "narc", 0);
    }
    char *cwd = getcwd(0, 0);
    if (cwd) {
        const char *pwd = var_get("PWD");
        char *resolved = pwd && pwd[0] == '/' ? realpath(pwd, 0) : 0;
        if (!resolved || strcmp(resolved, cwd)) var_set("PWD", cwd, VAR_EXPORT);
        free(resolved);
        free(cwd);
    }
    var_add_flags("PWD", VAR_EXPORT);
}
