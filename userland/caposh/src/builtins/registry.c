#include <caposh/builtins.h>
#include <caposh/shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct builtin table[] = {
    { ".", builtin_dot, BUILTIN_SPECIAL, ". file [arguments]" },
    { ":", builtin_colon, BUILTIN_SPECIAL, ": [arguments]" },
    { "[", builtin_test, 0, "[ expression ]" },
    { "alias", builtin_alias, 0, "alias [-p] [name[=value] ...]" },
    { "bg", builtin_bg, 0, "bg [job ...]" },
    { "break", builtin_break, BUILTIN_SPECIAL, "break [n]" },
    { "builtin", builtin_builtin, 0, "builtin [shell-builtin [arguments ...]]" },
    { "cd", builtin_cd, 0, "cd [-L|-P] [dir]" },
    { "command", builtin_command, 0, "command [-pVv] command [arguments ...]" },
    { "continue", builtin_continue, BUILTIN_SPECIAL, "continue [n]" },
    { "declare", builtin_declare, 0, "declare [-fFgilprux] [name[=value] ...]" },
    { "disown", builtin_disown, 0, "disown [-a] [job ...]" },
    { "echo", builtin_echo, 0, "echo [-neE] [argument ...]" },
    { "eval", builtin_eval, BUILTIN_SPECIAL, "eval [argument ...]" },
    { "exec", builtin_exec, BUILTIN_SPECIAL, "exec [-cl] [-a name] [command [arguments ...]]" },
    { "exit", builtin_exit, BUILTIN_SPECIAL, "exit [n]" },
    { "export", builtin_export, BUILTIN_SPECIAL, "export [-fnp] [name[=value] ...]" },
    { "false", builtin_false, 0, "false" },
    { "fg", builtin_fg, 0, "fg [job]" },
    { "getopts", builtin_getopts, 0, "getopts optstring name [argument ...]" },
    { "hash", builtin_hash, 0, "hash [-r] [name ...]" },
    { "help", builtin_help, 0, "help [pattern ...]" },
    { "history", builtin_history, 0, "history [-c] [-d offset] [n]" },
    { "jobs", builtin_jobs, 0, "jobs [-lnprs] [job ...]" },
    { "kill", builtin_kill, 0, "kill [-s signal | -signal] pid | job ... or kill -l [signal]" },
    { "let", builtin_let, 0, "let expression [expression ...]" },
    { "local", builtin_local, 0, "local [name[=value] ...]" },
    { "printf", builtin_printf, 0, "printf [-v var] format [arguments]" },
    { "pwd", builtin_pwd, 0, "pwd [-LP]" },
    { "read", builtin_read, 0,
      "read [-ers] [-d delim] [-n nchars] [-p prompt] [-t timeout] [-u fd] [name ...]" },
    { "readonly", builtin_readonly, BUILTIN_SPECIAL, "readonly [-fp] [name[=value] ...]" },
    { "return", builtin_return, BUILTIN_SPECIAL, "return [n]" },
    { "set", builtin_set, BUILTIN_SPECIAL, "set [-abCefhmnuvx] [-o option] [--] [argument ...]" },
    { "shift", builtin_shift, BUILTIN_SPECIAL, "shift [n]" },
    { "source", builtin_dot, 0, "source file [arguments]" },
    { "test", builtin_test, 0, "test [expression]" },
    { "times", builtin_times, BUILTIN_SPECIAL, "times" },
    { "trap", builtin_trap, BUILTIN_SPECIAL, "trap [-lp] [[action] signal ...]" },
    { "true", builtin_true, 0, "true" },
    { "type", builtin_type, 0, "type [-afptP] name [name ...]" },
    { "typeset", builtin_declare, 0, "typeset [-fFgilprux] [name[=value] ...]" },
    { "umask", builtin_umask, 0, "umask [-pS] [mode]" },
    { "unalias", builtin_unalias, 0, "unalias [-a] name [name ...]" },
    { "unset", builtin_unset, BUILTIN_SPECIAL, "unset [-fv] [name ...]" },
    { "wait", builtin_wait, 0, "wait [-n] [id ...]" },
};

static int compare(const void *key, const void *entry) {
    return strcmp(key, ((const struct builtin *)entry)->name);
}

const struct builtin *builtin_find(const char *name) {
    return bsearch(name, table, sizeof(table) / sizeof(table[0]), sizeof(table[0]), compare);
}

const struct builtin *builtin_table(int *count) {
    *count = (int)(sizeof(table) / sizeof(table[0]));
    return table;
}

int builtin_usage(const char *name) {
    const struct builtin *builtin = builtin_find(name);
    if (builtin) fprintf(stderr, "%s: usage: %s\n", name, builtin->usage);
    return 2;
}
