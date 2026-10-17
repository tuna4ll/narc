#pragma once

#include <caposh/ast.h>
#include <sys/types.h>

#define EVAL_EXIT        0x01
#define EVAL_TESTED      0x02
#define EVAL_BACKGROUND  0x04
#define RUN_NO_FUNCTIONS 0x10

struct function {
    struct function *next;
    char *name;
    struct node *definition;
    struct arena arena;
    int references;
    int removed;
};

int evaluate(struct node *node, int flags);
int evaluate_string(const char *text, const char *name, int flags);
int run_command(int argc, char **argv, int flags);
char *command_substitute(const char *text);
_Noreturn void exec_external(char **argv, char **environment);
_Noreturn void exec_resolved(char *path, char **argv, char **environment);
void xtrace(char **argv, int count, struct word *assignments);

struct function *function_find(const char *name);
void function_define(struct node *definition);
int function_remove(const char *name);
int function_call(struct function *function, int argc, char **argv);
struct function **functions_sorted(int *count);
