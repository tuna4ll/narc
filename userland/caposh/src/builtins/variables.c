#include <caposh/arith.h>
#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/parser.h>
#include <caposh/path.h>
#include <caposh/printer.h>
#include <caposh/shell.h>
#include <caposh/vars.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void print_function(const struct function *function) {
    struct arena_mark mark = arena_mark(stack);
    printf("%s\n", print_source(function->definition, 0));
    arena_release(stack, mark);
}

static int valid_name(const char *builtin, const char *text, size_t length) {
    if (is_name_prefix(text, length)) return 1;
    builtin_error(builtin, "`%s': not a valid identifier", text);
    return 0;
}

static int declare_one(const char *builtin, const char *argument, int flags, int remove,
                       int local) {
    size_t length = strcspn(argument, "=");
    int append = length && argument[length] == '=' && argument[length - 1] == '+';
    size_t name_length = append ? length - 1 : length;
    if (!valid_name(builtin, argument, name_length)) return 1;
    char *name = ststrndup(argument, name_length);
    if (local && var_make_local(name) != 0) return 1;
    if (remove) var_remove_flags(name, remove);
    if (flags & ~VAR_READONLY) var_add_flags(name, flags & ~VAR_READONLY);
    if (argument[length] == '=') {
        int status = append ? var_assign(argument, 0) : var_set(name, argument + length + 1, 0);
        if (status) return 1;
    }
    if (flags & VAR_READONLY) var_add_flags(name, VAR_READONLY);
    if ((flags & VAR_EXPORT) && !var_lookup(name)) var_add_flags(name, VAR_EXPORT);
    return 0;
}

static void print_flags_line(const struct var *var) {
    char flags[8];
    size_t n = 0;
    if (var->flags & VAR_INTEGER) flags[n++] = 'i';
    if (var->flags & VAR_LOWER) flags[n++] = 'l';
    if (var->flags & VAR_READONLY) flags[n++] = 'r';
    if (var->flags & VAR_UPPER) flags[n++] = 'u';
    if (var->flags & VAR_EXPORT) flags[n++] = 'x';
    flags[n] = 0;
    printf("declare -%s ", n ? flags : "-");
    var_print_assignment(var, 0);
}

static void print_matching(int mask, const char *prefix) {
    size_t count;
    struct var **vars = vars_sorted(&count);
    for (size_t i = 0; i < count; i++) {
        if (mask && !(vars[i]->flags & mask)) continue;
        if (prefix) var_print_assignment(vars[i], prefix);
        else print_flags_line(vars[i]);
    }
}

int builtin_export(int argc, char **argv) {
    int option, remove = 0, functions = 0, print = 0;
    while ((option = getopt(argc, argv, "fnp")) != -1) {
        if (option == 'n') remove = 1;
        else if (option == 'f') functions = 1;
        else if (option == 'p') print = 1;
        else return builtin_usage("export");
    }
    if (optind == argc || print) {
        print_matching(VAR_EXPORT, "export ");
        return 0;
    }
    int status = 0;
    for (int i = optind; i < argc; i++) {
        if (functions) {
            if (!function_find(argv[i])) {
                builtin_error("export", "%s: not a function", argv[i]);
                status = 1;
            }
            continue;
        }
        status |= remove ? declare_one("export", argv[i], 0, VAR_EXPORT, 0)
                         : declare_one("export", argv[i], VAR_EXPORT, 0, 0);
    }
    return status;
}

int builtin_readonly(int argc, char **argv) {
    int option;
    while ((option = getopt(argc, argv, "fp")) != -1)
        if (option == '?') return builtin_usage("readonly");
    if (optind == argc) {
        print_matching(VAR_READONLY, "readonly ");
        return 0;
    }
    int status = 0;
    for (int i = optind; i < argc; i++) status |= declare_one("readonly", argv[i], VAR_READONLY, 0, 0);
    return status;
}

int builtin_local(int argc, char **argv) {
    if (!shell.function_depth) {
        builtin_error("local", "can only be used in a function");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) continue;
        status |= declare_one("local", argv[i], 0, 0, 1);
    }
    return status;
}

int builtin_declare(int argc, char **argv) {
    int add = 0, remove = 0, functions = 0, names_only = 0, print = 0, global = 0;
    int index = 1;
    for (; index < argc; index++) {
        const char *argument = argv[index];
        if ((argument[0] != '-' && argument[0] != '+') || !argument[1]) break;
        if (!strcmp(argument, "--")) {
            index++;
            break;
        }
        int enable = argument[0] == '-';
        for (const char *flag = argument + 1; *flag; flag++) {
            int bit = 0;
            switch (*flag) {
            case 'x': bit = VAR_EXPORT; break;
            case 'r': bit = VAR_READONLY; break;
            case 'i': bit = VAR_INTEGER; break;
            case 'l': bit = VAR_LOWER; break;
            case 'u': bit = VAR_UPPER; break;
            case 'f': functions = 1; break;
            case 'F': functions = names_only = 1; break;
            case 'p': print = 1; break;
            case 'g': global = 1; break;
            default: return builtin_usage(argv[0]);
            }
            if (enable) add |= bit;
            else remove |= bit;
        }
    }
    if (functions) {
        int count;
        struct function **list = functions_sorted(&count);
        int status = 0;
        if (index == argc) {
            for (int i = 0; i < count; i++) {
                if (names_only) printf("declare -f %s\n", list[i]->name);
                else print_function(list[i]);
            }
            return 0;
        }
        for (int i = index; i < argc; i++) {
            struct function *function = function_find(argv[i]);
            if (!function) status = 1;
            else if (names_only) printf("%s\n", function->name);
            else print_function(function);
        }
        return status;
    }
    if (index == argc) {
        print_matching(add, 0);
        return 0;
    }
    int status = 0;
    int local = shell.function_depth && !global;
    for (int i = index; i < argc; i++) {
        if (print) {
            struct var *var = var_lookup(argv[i]);
            if (var) print_flags_line(var);
            else status = 1;
            continue;
        }
        status |= declare_one(argv[0], argv[i], add, remove, local);
    }
    return status;
}

int builtin_unset(int argc, char **argv) {
    int option, functions = 0, variables = 0;
    while ((option = getopt(argc, argv, "fv")) != -1) {
        if (option == 'f') functions = 1;
        else if (option == 'v') variables = 1;
        else return builtin_usage("unset");
    }
    int status = 0;
    for (int i = optind; i < argc; i++) {
        if (functions) {
            function_remove(argv[i]);
            continue;
        }
        if (!is_name(argv[i])) {
            builtin_error("unset", "`%s': not a valid identifier", argv[i]);
            status = 1;
            continue;
        }
        if (var_lookup(argv[i]) || variables) {
            if (var_unset(argv[i]) != 0) status = 1;
            if (!strcmp(argv[i], "PATH")) path_hash_clear();
        } else {
            function_remove(argv[i]);
        }
    }
    return status;
}

int builtin_let(int argc, char **argv) {
    if (argc < 2) {
        builtin_error("let", "expression expected");
        return 1;
    }
    long long value = 0;
    for (int i = 1; i < argc; i++) value = arith_evaluate(argv[i]);
    return value ? 0 : 1;
}
