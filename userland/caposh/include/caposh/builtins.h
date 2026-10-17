#pragma once

#define BUILTIN_SPECIAL 0x01

struct builtin {
    const char *name;
    int (*run)(int argc, char **argv);
    int flags;
    const char *usage;
};

const struct builtin *builtin_find(const char *name);
const struct builtin *builtin_table(int *count);
int builtin_usage(const char *name);

int builtin_alias(int argc, char **argv);
int builtin_bg(int argc, char **argv);
int builtin_break(int argc, char **argv);
int builtin_builtin(int argc, char **argv);
int builtin_cd(int argc, char **argv);
int builtin_colon(int argc, char **argv);
int builtin_command(int argc, char **argv);
int builtin_continue(int argc, char **argv);
int builtin_declare(int argc, char **argv);
int builtin_disown(int argc, char **argv);
int builtin_dot(int argc, char **argv);
int builtin_echo(int argc, char **argv);
int builtin_eval(int argc, char **argv);
int builtin_exec(int argc, char **argv);
int builtin_exit(int argc, char **argv);
int builtin_export(int argc, char **argv);
int builtin_false(int argc, char **argv);
int builtin_fg(int argc, char **argv);
int builtin_getopts(int argc, char **argv);
int builtin_hash(int argc, char **argv);
int builtin_help(int argc, char **argv);
int builtin_history(int argc, char **argv);
int builtin_jobs(int argc, char **argv);
int builtin_kill(int argc, char **argv);
int builtin_let(int argc, char **argv);
int builtin_local(int argc, char **argv);
int builtin_printf(int argc, char **argv);
int builtin_pwd(int argc, char **argv);
int builtin_read(int argc, char **argv);
int builtin_readonly(int argc, char **argv);
int builtin_return(int argc, char **argv);
int builtin_set(int argc, char **argv);
int builtin_shift(int argc, char **argv);
int builtin_test(int argc, char **argv);
int builtin_times(int argc, char **argv);
int builtin_trap(int argc, char **argv);
int builtin_true(int argc, char **argv);
int builtin_type(int argc, char **argv);
int builtin_umask(int argc, char **argv);
int builtin_unalias(int argc, char **argv);
int builtin_unset(int argc, char **argv);
int builtin_wait(int argc, char **argv);

int test_evaluate(int argc, char **argv, int *status);
int test_unary(const char *op, const char *operand);
int test_binary(const char *left, const char *op, const char *right, int *error);
