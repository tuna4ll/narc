#pragma once

#include <caposh/memory.h>
#include <stdint.h>
#include <sys/types.h>

#define CAPOSH_VERSION "1.0.0"
#define CAPOSH_NAME "caposh"

struct node;

struct shell {
    const char *name;
    const char *script;
    int interactive;
    int session_interactive;
    int login;
    int subshell;
    int status;
    int line;
    pid_t pid;
    pid_t last_background;
    int loop_depth;
    int break_count;
    int continue_count;
    int function_depth;
    int returning;
    int source_depth;
    int evaluating_trap;
    int tested;
    char **arguments;
    int argument_count;
    int arguments_owned;
    int64_t started;
};

extern struct shell shell;

void shell_exit(int status) __attribute__((noreturn));
