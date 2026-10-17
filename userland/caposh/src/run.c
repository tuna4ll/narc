#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/input.h>
#include <caposh/jobs.h>
#include <caposh/options.h>
#include <caposh/parser.h>
#include <caposh/path.h>
#include <caposh/positional.h>
#include <caposh/redirect.h>
#include <caposh/run.h>
#include <caposh/shell.h>
#include <caposh/trap.h>
#include <caposh/vars.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define EOF_WARNINGS 10

static int eof_warnings;
static int stopped_warning;

static int end_of_input(int top_level) {
    if (!top_level || !shell.interactive) return 1;
    if (option_enabled(OPTION_IGNOREEOF) && ++eof_warnings < EOF_WARNINGS) {
        fprintf(stderr, "Use \"exit\" to leave the shell.\n");
        return 0;
    }
    if (jobs_stopped() && !stopped_warning) {
        stopped_warning = 1;
        fprintf(stderr, "There are stopped jobs.\n");
        return 0;
    }
    return 1;
}

int run_loop(int top_level) {
    volatile int status = 0;
    for (;;) {
        struct arena_mark mark = arena_mark(stack);
        if (top_level && shell.interactive) {
            jobs_notify();
            if (trap_pending()) trap_run_pending();
        }
        volatile int finished = 0;
        struct handler frame;
        int kind = TRY(&frame);
        if (!kind) {
            struct node *node;
            enum parse_result result = parse_command_line(stack, &node);
            if (result == PARSE_EOF) {
                finished = 1;
            } else if (result == PARSE_COMMAND) {
                eof_warnings = 0;
                if (!option_enabled(OPTION_NOEXEC) || shell.interactive) status = evaluate(node, 0);
                if (!option_enabled(OPTION_ERREXIT) || !shell.status) stopped_warning = 0;
            }
        }
        END_TRY(&frame);
        arena_release(stack, mark);
        if (kind) {
            if (kind == EXCEPTION_EXIT || !top_level) raise_exception(kind);
            if (!shell.interactive) shell_exit(exception_status);
            shell.status = kind == EXCEPTION_INTERRUPT ? 130 : exception_status;
            status = shell.status;
            if (kind == EXCEPTION_INTERRUPT) fputc('\n', stderr);
            input_discard_line();
            shell.loop_depth = shell.break_count = shell.continue_count = 0;
            shell.returning = 0;
            interrupted = 0;
            continue;
        }
        if (finished) {
            if (end_of_input(top_level)) break;
            continue;
        }
        if (shell.returning && !top_level) break;
    }
    return status;
}

int source_file(const char *path, int argc, char **argv, int quiet) {
    char *resolved = strchr(path, '/') ? (char *)path : path_search(path, 0);
    if (!resolved) {
        if (access(path, R_OK) == 0) resolved = (char *)path;
    }
    int fd = resolved ? open(resolved, O_RDONLY | O_CLOEXEC) : -1;
    if (fd < 0) {
        if (!quiet) {
            errno = resolved ? errno : ENOENT;
            sh_perror(path);
        }
        return 1;
    }
    fd = redirect_move_high(fd);
    char **saved_arguments = shell.arguments;
    int saved_count = shell.argument_count;
    int saved_owned = shell.arguments_owned;
    if (argc > 0) {
        shell.arguments = argv;
        shell.argument_count = argc;
        shell.arguments_owned = 0;
    }
    int level = input_level();
    input_push_file(fd, path);
    shell.source_depth++;
    volatile int status = 0;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) status = run_loop(0);
    END_TRY(&frame);
    shell.source_depth--;
    input_unwind(level);
    close(fd);
    if (argc > 0) {
        positional_release();
        shell.arguments = saved_arguments;
        shell.argument_count = saved_count;
        shell.arguments_owned = saved_owned;
    }
    if (shell.returning) {
        shell.returning = 0;
        status = shell.status;
    }
    if (kind) raise_exception(kind);
    return status;
}
