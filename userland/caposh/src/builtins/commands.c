#include <caposh/alias.h>
#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/jobs.h>
#include <caposh/lineedit.h>
#include <caposh/path.h>
#include <caposh/printer.h>
#include <caposh/run.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/trap.h>
#include <caposh/vars.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const keywords[] = {
    "!", "[[", "]]", "case", "do", "done", "elif", "else", "esac", "fi", "for", "function",
    "if", "in", "then", "until", "while", "{", "}", 0,
};

static int is_keyword(const char *name) {
    for (int i = 0; keywords[i]; i++)
        if (!strcmp(keywords[i], name)) return 1;
    return 0;
}

int builtin_eval(int argc, char **argv) {
    if (argc < 2) return 0;
    struct strbuf text;
    strbuf_init(&text, stack);
    for (int i = 1; i < argc; i++) {
        if (i > 1) strbuf_putc(&text, ' ');
        strbuf_puts(&text, argv[i]);
    }
    return evaluate_string(strbuf_string(&text), 0, 0);
}

int builtin_dot(int argc, char **argv) {
    int index = 1;
    if (index < argc && !strcmp(argv[index], "--")) index++;
    if (index >= argc) {
        builtin_error(argv[0], "filename argument required");
        return 2;
    }
    return source_file(argv[index], argc - index - 1, argv + index + 1, 0);
}

int builtin_exec(int argc, char **argv) {
    int index = 1, clear = 0, login = 0;
    const char *name = 0;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++) {
        if (!strcmp(argv[index], "--")) {
            index++;
            break;
        }
        for (const char *flag = argv[index] + 1; *flag; flag++) {
            if (*flag == 'c') clear = 1;
            else if (*flag == 'l') login = 1;
            else if (*flag == 'a' && index + 1 < argc) name = argv[++index];
            else return builtin_usage("exec");
        }
    }
    if (index >= argc) return 0;
    char **arguments = argv + index;
    char *path = strchr(arguments[0], '/') ? arguments[0] : path_search(arguments[0], 1);
    if (!path) {
        builtin_error("exec", "%s: not found", arguments[0]);
        if (!shell.interactive) shell_exit(127);
        return 127;
    }
    if (name || login) {
        struct strbuf zero;
        strbuf_init(&zero, stack);
        if (login) strbuf_putc(&zero, '-');
        strbuf_puts(&zero, name ? name : arguments[0]);
        arguments[0] = strbuf_string(&zero);
    }
    char *empty[] = { 0 };
    lineedit_restore_terminal();
    fflush(stdout);
    fflush(stderr);
    exec_resolved(path, arguments, clear ? empty : vars_environment(stack));
}

static int describe(const char *name, int verbose, int path_only, int all, int type_only) {
    int found = 0;
    if (!path_only) {
        struct alias *alias = alias_find(name);
        if (alias) {
            found = 1;
            if (type_only) printf("alias\n");
            else if (verbose) printf("%s is aliased to `%s'\n", name, alias->value);
            else printf("alias %s='%s'\n", name, alias->value);
            if (!all) return 0;
        }
        if (is_keyword(name)) {
            found = 1;
            if (type_only) printf("keyword\n");
            else if (verbose) printf("%s is a shell keyword\n", name);
            else printf("%s\n", name);
            if (!all) return 0;
        }
        struct function *function = function_find(name);
        if (function) {
            found = 1;
            if (type_only) printf("function\n");
            else if (verbose) {
                struct arena_mark mark = arena_mark(stack);
                printf("%s is a function\n%s\n", name, print_source(function->definition, 0));
                arena_release(stack, mark);
            } else printf("%s\n", name);
            if (!all) return 0;
        }
        const struct builtin *builtin = builtin_find(name);
        if (builtin) {
            found = 1;
            if (type_only) printf("builtin\n");
            else if (verbose) printf("%s is a shell %sbuiltin\n", name,
                                     builtin->flags & BUILTIN_SPECIAL ? "special " : "");
            else printf("%s\n", name);
            if (!all) return 0;
        }
    }
    char *path = strchr(name, '/') ? (path_executable(name) ? (char *)name : 0) : path_search(name, 0);
    if (path) {
        found = 1;
        if (type_only) printf("file\n");
        else if (verbose) printf("%s is %s\n", name, path);
        else printf("%s\n", path);
    }
    return found ? 0 : 1;
}

int builtin_command(int argc, char **argv) {
    int option, verbose = 0, brief = 0, default_path = 0;
    while ((option = getopt(argc, argv, "pvV")) != -1) {
        if (option == 'v') brief = 1;
        else if (option == 'V') verbose = 1;
        else if (option == 'p') default_path = 1;
        else return builtin_usage("command");
    }
    if (optind >= argc) return 0;
    if (brief || verbose) {
        int status = 0;
        for (int i = optind; i < argc; i++) {
            if (describe(argv[i], verbose, 0, 0, 0)) {
                if (verbose) builtin_error("command", "%s: not found", argv[i]);
                status = 1;
            }
        }
        return status;
    }
    char *saved_path = 0;
    if (default_path) {
        const char *path = var_get("PATH");
        saved_path = path ? xstrdup(path) : 0;
        var_set("PATH", "/bin:/sbin:/usr/bin", 0);
    }
    int status = run_command(argc - optind, argv + optind, RUN_NO_FUNCTIONS);
    if (default_path) {
        if (saved_path) var_set("PATH", saved_path, 0);
        else var_unset("PATH");
        free(saved_path);
    }
    return status;
}

int builtin_builtin(int argc, char **argv) {
    if (argc < 2) return 0;
    const struct builtin *builtin = builtin_find(argv[1]);
    if (!builtin) {
        builtin_error("builtin", "%s: not a shell builtin", argv[1]);
        return 1;
    }
    optind = 0;
    return builtin->run(argc - 1, argv + 1);
}

int builtin_type(int argc, char **argv) {
    int option, all = 0, path_only = 0, type_only = 0;
    while ((option = getopt(argc, argv, "afptP")) != -1) {
        if (option == 'a') all = 1;
        else if (option == 'p' || option == 'P') path_only = 1;
        else if (option == 't') type_only = 1;
        else if (option == 'f') continue;
        else return builtin_usage("type");
    }
    int status = 0;
    for (int i = optind; i < argc; i++) {
        if (describe(argv[i], !path_only, path_only, all, type_only)) {
            if (!type_only && !path_only) builtin_error("type", "%s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

int builtin_hash(int argc, char **argv) {
    int option;
    while ((option = getopt(argc, argv, "r")) != -1) {
        if (option == 'r') path_hash_clear();
        else return builtin_usage("hash");
    }
    if (optind == argc) {
        if (argc == 1) path_hash_print();
        return 0;
    }
    int status = 0;
    for (int i = optind; i < argc; i++) {
        if (builtin_find(argv[i]) || function_find(argv[i])) continue;
        if (path_hash_add(argv[i]) != 0) {
            builtin_error("hash", "%s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

int builtin_times(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("0m0.000s 0m0.000s\n0m0.000s 0m0.000s\n");
    return 0;
}

int builtin_help(int argc, char **argv) {
    int count;
    const struct builtin *table = builtin_table(&count);
    if (argc < 2) {
        printf("caposh, version %s\n", CAPOSH_VERSION);
        printf("These shell commands are defined internally. Type `help name' to find out more.\n\n");
        for (int i = 0; i < count; i++) printf(" %s\n", table[i].usage);
        return 0;
    }
    int status = 1;
    for (int arg = 1; arg < argc; arg++) {
        for (int i = 0; i < count; i++) {
            if (fnmatch(argv[arg], table[i].name, 0)) continue;
            printf("%s: %s\n", table[i].name, table[i].usage);
            status = 0;
        }
    }
    if (status) builtin_error("help", "no help topics match `%s'", argv[argc - 1]);
    return status;
}
