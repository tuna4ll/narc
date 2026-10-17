#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/jobs.h>
#include <caposh/memory.h>
#include <caposh/options.h>
#include <caposh/positional.h>
#include <caposh/shell.h>
#include <caposh/vars.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void positional_set(int count, char **values) {
    char **copy = xmalloc(((size_t)count + 1) * sizeof(char *));
    for (int i = 0; i < count; i++) copy[i] = xstrdup(values[i]);
    copy[count] = 0;
    positional_release();
    shell.arguments = copy;
    shell.argument_count = count;
    shell.arguments_owned = 1;
}

void positional_release(void) {
    if (!shell.arguments_owned) return;
    for (int i = 0; i < shell.argument_count; i++) free(shell.arguments[i]);
    free(shell.arguments);
    shell.arguments = 0;
    shell.argument_count = 0;
    shell.arguments_owned = 0;
}

static void apply(enum option option, int enable) {
    if (option == OPTION_MONITOR) {
        if (enable && !jobs_monitor()) jobs_init();
        if (!enable) jobs_disable();
        return;
    }
    option_set(option, enable);
}

int builtin_set(int argc, char **argv) {
    if (argc == 1) {
        size_t count;
        struct var **vars = vars_sorted(&count);
        for (size_t i = 0; i < count; i++)
            if (!(vars[i]->flags & VAR_UNSET)) var_print_assignment(vars[i], 0);
        return 0;
    }
    int index = 1, replace = 0;
    for (; index < argc; index++) {
        char *argument = argv[index];
        if (!strcmp(argument, "--")) {
            index++;
            replace = 1;
            break;
        }
        if (!strcmp(argument, "-")) {
            option_set(OPTION_XTRACE, 0);
            option_set(OPTION_VERBOSE, 0);
            index++;
            replace = index < argc;
            break;
        }
        if ((argument[0] != '-' && argument[0] != '+') || !argument[1]) {
            replace = 1;
            break;
        }
        int enable = argument[0] == '-';
        for (char *flag = argument + 1; *flag; flag++) {
            if (*flag == 'o') {
                if (index + 1 >= argc) {
                    option_print(!enable);
                    continue;
                }
                int option = option_by_name(argv[++index]);
                if (option < 0) {
                    builtin_error("set", "%s: invalid option name", argv[index]);
                    return 2;
                }
                apply(option, enable);
                continue;
            }
            int option = option_by_letter(*flag);
            if (option < 0) {
                builtin_error("set", "%c%c: invalid option", enable ? '-' : '+', *flag);
                return builtin_usage("set");
            }
            apply(option, enable);
        }
    }
    if (replace) positional_set(argc - index, argv + index);
    return 0;
}

int builtin_shift(int argc, char **argv) {
    long count = 1;
    if (argc > 2) {
        builtin_error("shift", "too many arguments");
        return 1;
    }
    if (argc == 2) {
        char *end;
        count = strtol(argv[1], &end, 10);
        if (*end || count < 0) {
            builtin_error("shift", "%s: numeric argument required", argv[1]);
            return 1;
        }
    }
    if (count > shell.argument_count) {
        builtin_error("shift", "shift count out of range");
        return 1;
    }
    if (!count) return 0;
    positional_set(shell.argument_count - (int)count, shell.arguments + count);
    return 0;
}
