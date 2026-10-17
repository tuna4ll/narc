#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/jobs.h>
#include <caposh/shell.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int numeric(const char *text, long *value) {
    if (!*text) return 0;
    const char *s = text;
    if (*s == '-' || *s == '+') s++;
    if (!*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    *value = strtol(text, 0, 10);
    return 1;
}

static int loop_count(const char *name, int argc, char **argv, long *count) {
    *count = 1;
    if (argc > 2) {
        builtin_error(name, "too many arguments");
        return 1;
    }
    if (argc == 2 && (!numeric(argv[1], count) || *count < 1)) {
        builtin_error(name, "%s: loop count out of range", argv[1]);
        return 1;
    }
    return 0;
}

int builtin_break(int argc, char **argv) {
    long count;
    if (loop_count("break", argc, argv, &count)) return 1;
    if (!shell.loop_depth) return 0;
    shell.break_count = count > shell.loop_depth ? shell.loop_depth : (int)count;
    return 0;
}

int builtin_continue(int argc, char **argv) {
    long count;
    if (loop_count("continue", argc, argv, &count)) return 1;
    if (!shell.loop_depth) return 0;
    if (count > shell.loop_depth) count = shell.loop_depth;
    shell.continue_count = (int)count;
    return 0;
}

int builtin_return(int argc, char **argv) {
    long value = shell.status;
    if (argc > 2) {
        builtin_error("return", "too many arguments");
        return 2;
    }
    if (argc == 2 && !numeric(argv[1], &value)) {
        builtin_error("return", "%s: numeric argument required", argv[1]);
        value = 2;
    }
    if (!shell.function_depth && !shell.source_depth) {
        builtin_error("return", "can only `return' from a function or sourced script");
        return 1;
    }
    shell.status = (int)(value & 0xff);
    shell.returning = 1;
    return shell.status;
}

int builtin_exit(int argc, char **argv) {
    static int warned;
    long value = shell.status;
    if (argc > 2) {
        builtin_error("exit", "too many arguments");
        return 1;
    }
    if (argc == 2 && !numeric(argv[1], &value)) {
        builtin_error("exit", "%s: numeric argument required", argv[1]);
        value = 2;
    }
    if (shell.interactive && !shell.subshell && jobs_stopped() && !warned) {
        warned = 1;
        fprintf(stderr, "There are stopped jobs.\n");
        return 1;
    }
    if (shell.interactive && !shell.subshell) fprintf(stderr, "exit\n");
    shell.status = (int)(value & 0xff);
    raise_exception(EXCEPTION_EXIT);
}

int builtin_true(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}

int builtin_false(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}

int builtin_colon(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}
