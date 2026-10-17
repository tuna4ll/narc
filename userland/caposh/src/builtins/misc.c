#include <caposh/alias.h>
#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/history.h>
#include <caposh/parser.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/trap.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int builtin_alias(int argc, char **argv) {
    int index = 1;
    if (index < argc && !strcmp(argv[index], "-p")) index++;
    if (index >= argc) {
        alias_print_all();
        return 0;
    }
    int status = 0;
    for (int i = index; i < argc; i++) {
        char *equals = strchr(argv[i], '=');
        if (equals) {
            char *name = ststrndup(argv[i], (size_t)(equals - argv[i]));
            if (!*name || strpbrk(name, " \t\n/$`=|&;<>()'\"\\")) {
                builtin_error("alias", "`%s': invalid alias name", name);
                status = 1;
                continue;
            }
            alias_set(name, equals + 1);
            continue;
        }
        struct alias *alias = alias_find(argv[i]);
        if (alias) {
            alias_print(alias);
        } else {
            builtin_error("alias", "%s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

int builtin_unalias(int argc, char **argv) {
    if (argc < 2) return builtin_usage("unalias");
    if (!strcmp(argv[1], "-a")) {
        alias_clear();
        return 0;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (alias_remove(argv[i]) != 0) {
            builtin_error("unalias", "%s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

static void print_trap(int signal) {
    const char *action = trap_action(signal);
    if (!action) return;
    printf("trap -- %s %s\n", shell_quote(stack, action), signal_name(signal));
}

int builtin_trap(int argc, char **argv) {
    int index = 1;
    if (index < argc && !strcmp(argv[index], "--")) index++;
    if (index < argc && !strcmp(argv[index], "-l")) {
        for (int signal = 1; signal < NSIG; signal++)
            printf("%2d) SIG%s\n", signal, signal_name(signal));
        return 0;
    }
    if (index < argc && !strcmp(argv[index], "-p")) index++;
    if (index >= argc) {
        for (int signal = 0; signal < NSIG; signal++) print_trap(signal);
        return 0;
    }
    const char *action = argv[index];
    int first = index + 1;
    if (isdigit((unsigned char)action[0]) && signal_number(action) >= 0) {
        action = "-";
        first = index;
    }
    if (first >= argc) {
        int signal = signal_number(action);
        if (signal >= 0) {
            print_trap(signal);
            return 0;
        }
        return builtin_usage("trap");
    }
    int status = 0;
    for (int i = first; i < argc; i++) {
        int signal = signal_number(argv[i]);
        if (signal < 0 || trap_set(signal, !strcmp(action, "-") ? 0 : action) != 0) {
            builtin_error("trap", "%s: invalid signal specification", argv[i]);
            status = 1;
        }
    }
    return status;
}

static void symbolic(mode_t mask) {
    mode_t allowed = ~mask & 0777;
    static const char *const who = "ugo";
    for (int i = 0; i < 3; i++) {
        int shift = 6 - i * 3;
        printf("%c=", who[i]);
        if (allowed & (4 << shift)) putchar('r');
        if (allowed & (2 << shift)) putchar('w');
        if (allowed & (1 << shift)) putchar('x');
        if (i < 2) putchar(',');
    }
    putchar('\n');
}

static int parse_symbolic(const char *text, mode_t *mask) {
    mode_t allowed = ~*mask & 0777;
    for (const char *s = text; *s;) {
        int who = 0;
        for (; *s && strchr("ugoa", *s); s++)
            who |= *s == 'u' ? 0700 : *s == 'g' ? 0070 : *s == 'o' ? 0007 : 0777;
        if (!who) who = 0777;
        if (!*s || !strchr("+-=", *s)) return -1;
        char op = *s++;
        int bits = 0;
        for (; *s && strchr("rwx", *s); s++)
            bits |= *s == 'r' ? 0444 : *s == 'w' ? 0222 : 0111;
        bits &= who;
        if (op == '+') allowed |= (mode_t)bits;
        else if (op == '-') allowed &= ~(mode_t)bits;
        else allowed = (allowed & ~(mode_t)who) | (mode_t)bits;
        if (*s == ',') s++;
        else if (*s) return -1;
    }
    *mask = ~allowed & 0777;
    return 0;
}

int builtin_umask(int argc, char **argv) {
    int symbolic_output = 0, reusable = 0, index = 1;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++) {
        if (!strcmp(argv[index], "-S")) symbolic_output = 1;
        else if (!strcmp(argv[index], "-p")) reusable = 1;
        else return builtin_usage("umask");
    }
    mode_t mask = umask(0);
    umask(mask);
    if (index >= argc) {
        if (symbolic_output) symbolic(mask);
        else printf("%s%04o\n", reusable ? "umask " : "", (unsigned)mask);
        return 0;
    }
    const char *text = argv[index];
    if (isdigit((unsigned char)*text)) {
        char *end;
        long value = strtol(text, &end, 8);
        if (*end || value < 0 || value > 0777) {
            builtin_error("umask", "%s: octal number out of range", text);
            return 1;
        }
        mask = (mode_t)value;
    } else if (parse_symbolic(text, &mask) != 0) {
        builtin_error("umask", "%s: invalid symbolic mode", text);
        return 1;
    }
    umask(mask);
    return 0;
}

int builtin_getopts(int argc, char **argv) {
    if (argc < 3) return builtin_usage("getopts");
    const char *spec = argv[1];
    const char *name = argv[2];
    char **arguments = argc > 3 ? argv + 3 : shell.arguments;
    int count = argc > 3 ? argc - 3 : shell.argument_count;
    int silent = spec[0] == ':';
    const char *optind_text = var_get("OPTIND");
    long index = optind_text ? strtol(optind_text, 0, 10) : 1;
    static long position = 1;
    static long last_index = -1;
    if (index != last_index) position = 1;
    if (index < 1) index = 1;
    var_unset("OPTARG");
    if (index > count) {
        var_set(name, "?", 0);
        last_index = index;
        return 1;
    }
    const char *argument = arguments[index - 1];
    if (position == 1 && (argument[0] != '-' || !argument[1] || !strcmp(argument, "--"))) {
        if (!strcmp(argument, "--")) index++;
        var_set_integer("OPTIND", index);
        last_index = index;
        var_set(name, "?", 0);
        return 1;
    }
    char letter = argument[position++];
    const char *match = letter != ':' ? strchr(spec + silent, letter) : 0;
    char text[2] = { letter, 0 };
    int done_with_argument = !argument[position];
    if (!match) {
        if (silent) {
            var_set("OPTARG", text, 0);
        } else {
            fprintf(stderr, "%s: illegal option -- %c\n", shell.name, letter);
        }
        var_set(name, "?", 0);
    } else if (match[1] == ':') {
        if (argument[position]) {
            var_set("OPTARG", argument + position, 0);
            done_with_argument = 1;
        } else if (index < count) {
            var_set("OPTARG", arguments[index], 0);
            index++;
            done_with_argument = 1;
        } else {
            if (silent) {
                var_set(name, ":", 0);
                var_set("OPTARG", text, 0);
            } else {
                fprintf(stderr, "%s: option requires an argument -- %c\n", shell.name, letter);
                var_set(name, "?", 0);
            }
            done_with_argument = 1;
            if (done_with_argument) {
                index++;
                position = 1;
            }
            var_set_integer("OPTIND", index);
            last_index = index;
            return 0;
        }
        var_set(name, text, 0);
    } else {
        var_set(name, text, 0);
    }
    if (done_with_argument) {
        index++;
        position = 1;
    }
    var_set_integer("OPTIND", index);
    last_index = index;
    return 0;
}

int builtin_history(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "-c")) {
        history_clear();
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-d")) {
        long offset = strtol(argv[2], 0, 10);
        if (offset < (long)history_base() || offset >= (long)(history_base() + history_count())) {
            builtin_error("history", "%s: history position out of range", argv[2]);
            return 1;
        }
        history_delete((size_t)offset - history_base());
        return 0;
    }
    size_t count = history_count();
    size_t start = 0;
    if (argc > 1) {
        long last = strtol(argv[1], 0, 10);
        if (last >= 0 && (size_t)last < count) start = count - (size_t)last;
    }
    for (size_t i = start; i < count; i++) printf("%5zu  %s\n", history_base() + i, history_get(i));
    return 0;
}
