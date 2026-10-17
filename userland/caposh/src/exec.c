#include <caposh/arith.h>
#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/expand.h>
#include <caposh/input.h>
#include <caposh/jobs.h>
#include <caposh/options.h>
#include <caposh/parser.h>
#include <caposh/path.h>
#include <caposh/printer.h>
#include <caposh/redirect.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/trap.h>
#include <caposh/vars.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define FUNCTION_NEST_MAX 200

static int substitution_status = -1;

static int skipping(void) {
    return shell.break_count || shell.continue_count || shell.returning;
}

static void errexit(int status, int flags) {
    if (status && option_enabled(OPTION_ERREXIT) && !(flags & EVAL_TESTED) &&
        !shell.evaluating_trap) {
        shell.status = status;
        raise_exception(EXCEPTION_EXIT);
    }
}

static int with_redirects(struct node *node, int flags, int (*body)(struct node *, int)) {
    if (!node->redirects) return body(node, flags);
    struct redirection state = { 0 };
    if (redirect_apply(node->redirects, &state, 1) != 0) {
        redirect_restore(&state);
        return 1;
    }
    volatile int status = 1;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) status = body(node, flags & ~EVAL_EXIT);
    END_TRY(&frame);
    redirect_restore(&state);
    if (kind) raise_exception(kind);
    return status;
}

void xtrace(char **argv, int count, struct word *assignments) {
    if (!option_enabled(OPTION_XTRACE)) return;
    struct arena_mark mark = arena_mark(stack);
    const char *ps4 = var_get("PS4");
    struct strbuf line;
    strbuf_init(&line, stack);
    if (ps4) strbuf_puts(&line, expand_prompt_parameters(ps4));
    int first = 1;
    for (struct word *word = assignments; word; word = word->next) {
        size_t name = assignment_name_length(word->text);
        const char *value = word->text + name + 1;
        if (word->text[name] == '+') value++;
        if (!first) strbuf_putc(&line, ' ');
        strbuf_append(&line, word->text, (size_t)(value - word->text));
        strbuf_puts(&line, shell_quote(stack, expand_assignment(value)));
        first = 0;
    }
    for (int i = 0; i < count; i++) {
        if (!first) strbuf_putc(&line, ' ');
        strbuf_puts(&line, shell_quote(stack, argv[i]));
        first = 0;
    }
    strbuf_putc(&line, '\n');
    fflush(stdout);
    fputs(strbuf_string(&line), stderr);
    arena_release(stack, mark);
}

static int assign(struct word *assignments, int flags) {
    for (struct word *word = assignments; word; word = word->next) {
        shell.line = word->line;
        size_t length = assignment_name_length(word->text);
        char *name = ststrndup(word->text, length);
        int append = word->text[length] == '+';
        char *value = expand_assignment(word->text + length + 1 + append);
        int status;
        if (append) {
            struct strbuf joined;
            strbuf_init(&joined, stack);
            strbuf_puts(&joined, name);
            strbuf_puts(&joined, "+=");
            strbuf_puts(&joined, value);
            status = var_assign(strbuf_string(&joined), flags);
        } else {
            status = var_set(name, value, flags);
        }
        if (status != 0) return -1;
    }
    return 0;
}

static int temporary_assign(struct word *assignments) {
    for (struct word *word = assignments; word; word = word->next) {
        size_t length = assignment_name_length(word->text);
        char *name = ststrndup(word->text, length);
        if (var_make_local(name) != 0) return -1;
    }
    return assign(assignments, VAR_EXPORT);
}

_Noreturn void exec_external(char **argv, char **environment) {
    exec_resolved(strchr(argv[0], '/') ? argv[0] : path_search(argv[0], 1), argv, environment);
}

_Noreturn void exec_resolved(char *path, char **argv, char **environment) {
    if (!path) {
        sh_warning("%s: command not found", argv[0]);
        _exit(127);
    }
    input_sync();
    execve(path, argv, environment);
    int error = errno;
    if (error == ENOEXEC) {
        int count = 0;
        while (argv[count]) count++;
        char **script = stalloc(((size_t)count + 2) * sizeof(char *));
        script[0] = "caposh";
        script[1] = path;
        for (int i = 1; i <= count; i++) script[i + 1] = argv[i];
        execve("/bin/caposh", script, environment);
        error = errno;
    }
    errno = error;
    sh_perror(argv[0]);
    _exit(error == ENOENT ? 127 : 126);
}

static int run_builtin(const struct builtin *builtin, int argc, char **argv) {
    optind = 0;
    opterr = 0;
    int status = builtin->run(argc, argv);
    fflush(stdout);
    fflush(stderr);
    return status;
}

static char *resolve(const char *name) {
    return strchr(name, '/') ? (char *)name : path_search(name, 1);
}

static int run_external(struct node *node, int argc, char **argv, int flags) {
    (void)argc;
    char *path = resolve(argv[0]);
    if (!(flags & EVAL_EXIT) || trap_action(TRAP_EXIT)) {
        struct job *job = job_new(node ? print_text(node) : argv[0]);
        fflush(stdout);
        if (job_fork(job, 1)) return job_wait(job);
    }
    if (node) {
        struct redirection state = { 0 };
        if (redirect_apply(node->redirects, &state, 0) != 0) _exit(1);
        vars_push_scope();
        if (temporary_assign(node->simple.assignments) != 0) _exit(1);
    }
    exec_resolved(path, argv, vars_environment(stack));
}

int run_command(int argc, char **argv, int flags) {
    const struct builtin *builtin = builtin_find(argv[0]);
    if (builtin) return run_builtin(builtin, argc, argv);
    struct function *function = (flags & RUN_NO_FUNCTIONS) ? 0 : function_find(argv[0]);
    if (function) return function_call(function, argc, argv);
    return run_external(0, argc, argv, flags & ~EVAL_EXIT);
}

static int eval_simple_command(struct node *node, int flags);

static int eval_simple(struct node *node, int flags) {
    struct arena_mark mark = arena_mark(stack);
    volatile int status = 1;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) status = eval_simple_command(node, flags);
    END_TRY(&frame);
    arena_release(stack, mark);
    if (kind) raise_exception(kind);
    return status;
}

static int eval_simple_command(struct node *node, int flags) {
    substitution_status = -1;
    int argc;
    char **argv = expand_words(node->simple.words, &argc);
    if (!argc) {
        struct redirection state = { 0 };
        volatile int status = 0;
        xtrace(argv, 0, node->simple.assignments);
        if (assign(node->simple.assignments, 0) != 0) {
            if (!shell.interactive) {
                exception_status = 1;
                raise_exception(EXCEPTION_ERROR);
            }
            return 1;
        }
        if (node->redirects) {
            status = redirect_apply(node->redirects, &state, 1) ? 1 : 0;
            redirect_restore(&state);
        }
        if (!status && substitution_status >= 0) status = substitution_status;
        return status;
    }
    xtrace(argv, argc, node->simple.assignments);
    const struct builtin *builtin = builtin_find(argv[0]);
    struct function *function = 0;
    if (!builtin || !(builtin->flags & BUILTIN_SPECIAL)) function = function_find(argv[0]);
    if (builtin && builtin->run == builtin_exec && argc == 1) {
        struct redirection state = { 0 };
        if (redirect_apply(node->redirects, &state, 0) != 0) return 1;
        return assign(node->simple.assignments, 0) ? 1 : 0;
    }
    if (!builtin && !function) return run_external(node, argc, argv, flags);
    struct redirection state = { 0 };
    int special = builtin && (builtin->flags & BUILTIN_SPECIAL) && !function;
    if (redirect_apply(node->redirects, &state, 1) != 0) {
        redirect_restore(&state);
        if (special && !shell.interactive) {
            exception_status = 1;
            raise_exception(EXCEPTION_ERROR);
        }
        return 1;
    }
    volatile int status = 1;
    volatile int scoped = 0;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) {
        if (special) {
            if (assign(node->simple.assignments, 0) != 0) {
                exception_status = 1;
                raise_exception(EXCEPTION_ERROR);
            }
        } else if (node->simple.assignments) {
            vars_push_scope();
            scoped = 1;
            if (temporary_assign(node->simple.assignments) != 0) {
                exception_status = 1;
                raise_exception(EXCEPTION_ERROR);
            }
        }
        status = function ? function_call(function, argc, argv) : run_builtin(builtin, argc, argv);
    }
    END_TRY(&frame);
    if (scoped) vars_pop_scope();
    redirect_restore(&state);
    if (kind) raise_exception(kind);
    return status;
}

static void close_pipe(int fds[2]) {
    if (fds[0] >= 0) close(fds[0]);
    if (fds[1] >= 0) close(fds[1]);
}

static int eval_pipeline(struct node *node, int flags) {
    int count = node->pipeline.count;
    struct job *job = job_new(print_text(node));
    int input = -1;
    fflush(stdout);
    for (int i = 0; i < count; i++) {
        int fds[2] = { -1, -1 };
        if (i + 1 < count && pipe2(fds, O_CLOEXEC) != 0) {
            sh_perror("pipe");
            if (input >= 0) close(input);
            break;
        }
        pid_t pid = job_fork(job, !(flags & EVAL_BACKGROUND));
        if (pid == 0) {
            if (input >= 0) {
                dup2(input, 0);
                close(input);
            }
            if (fds[1] >= 0) {
                dup2(fds[1], 1);
                if (node->pipeline.stderr_mask & (1 << i)) dup2(fds[1], 2);
            }
            close_pipe(fds);
            evaluate(node->pipeline.commands[i], EVAL_EXIT);
            shell_exit(shell.status);
        }
        if (input >= 0) close(input);
        if (fds[1] >= 0) close(fds[1]);
        input = fds[0];
    }
    if (flags & EVAL_BACKGROUND) {
        job_started_background(job);
        return 0;
    }
    return job_wait(job);
}

static int eval_background(struct node *node, int flags) {
    (void)flags;
    struct node *body = node->unary.body;
    if (body->type == NODE_PIPELINE) return eval_pipeline(body, EVAL_BACKGROUND);
    struct job *job = job_new(print_text(body));
    fflush(stdout);
    pid_t pid = job_fork(job, 0);
    if (pid == 0) {
        evaluate(body, EVAL_EXIT);
        shell_exit(shell.status);
    }
    job_started_background(job);
    return 0;
}

static int eval_subshell(struct node *node, int flags) {
    if (flags & EVAL_EXIT) {
        struct redirection state = { 0 };
        if (redirect_apply(node->redirects, &state, 0) != 0) shell_exit(1);
        evaluate(node->unary.body, EVAL_EXIT);
        shell_exit(shell.status);
    }
    struct job *job = job_new(print_text(node));
    fflush(stdout);
    pid_t pid = job_fork(job, 1);
    if (pid == 0) {
        struct redirection state = { 0 };
        if (redirect_apply(node->redirects, &state, 0) != 0) shell_exit(1);
        evaluate(node->unary.body, EVAL_EXIT);
        shell_exit(shell.status);
    }
    return job_wait(job);
}

static int group_body(struct node *node, int flags) {
    return evaluate(node->unary.body, flags);
}

static int if_body(struct node *node, int flags) {
    int condition = evaluate(node->if_clause.condition, (flags | EVAL_TESTED) & ~EVAL_EXIT);
    if (skipping()) return condition;
    if (!condition) return node->if_clause.then_part ? evaluate(node->if_clause.then_part, flags) : 0;
    if (node->if_clause.else_part) return evaluate(node->if_clause.else_part, flags);
    return 0;
}

static int loop_control(int *status) {
    (void)status;
    if (shell.break_count) {
        shell.break_count--;
        return 1;
    }
    if (shell.continue_count) {
        if (--shell.continue_count) return 1;
        return 0;
    }
    return shell.returning;
}

static int loop_body(struct node *node, int flags) {
    int until = node->type == NODE_UNTIL, status = 0;
    shell.loop_depth++;
    flags &= ~EVAL_EXIT;
    for (;;) {
        int condition = evaluate(node->loop.condition, flags | EVAL_TESTED);
        if (skipping()) {
            if (loop_control(&status)) break;
            continue;
        }
        if (until ? !condition : condition) break;
        status = evaluate(node->loop.body, flags);
        if (skipping() && loop_control(&status)) break;
        check_interrupt();
    }
    shell.loop_depth--;
    return status;
}

static int for_body(struct node *node, int flags) {
    int count, status = 0;
    char **items;
    if (node->for_clause.has_items) {
        items = expand_words(node->for_clause.items, &count);
    } else {
        items = shell.arguments;
        count = shell.argument_count;
    }
    shell.loop_depth++;
    flags &= ~EVAL_EXIT;
    for (int i = 0; i < count; i++) {
        if (var_set(node->for_clause.name, items[i], 0) != 0) {
            status = 1;
            break;
        }
        status = evaluate(node->for_clause.body, flags);
        if (skipping() && loop_control(&status)) break;
        check_interrupt();
    }
    shell.loop_depth--;
    return status;
}

static long long arith_text(const char *text) {
    if (!text) return 1;
    struct arena_mark mark = arena_mark(stack);
    char *expanded = expand_string(text, EXPAND_NOSPLIT);
    const char *s = expanded;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    long long value = *s ? arith_evaluate(expanded) : 1;
    arena_release(stack, mark);
    return value;
}

static int arith_for_body(struct node *node, int flags) {
    int status = 0;
    shell.loop_depth++;
    flags &= ~EVAL_EXIT;
    if (node->arith_for.init) arith_text(node->arith_for.init);
    for (;;) {
        if (!arith_text(node->arith_for.condition)) break;
        status = evaluate(node->arith_for.body, flags);
        if (skipping() && loop_control(&status)) break;
        check_interrupt();
        if (node->arith_for.step) arith_text(node->arith_for.step);
    }
    shell.loop_depth--;
    return status;
}

static int case_body(struct node *node, int flags) {
    char *subject = expand_string(node->case_clause.subject->text, EXPAND_TILDE);
    int status = 0, fallthrough = 0;
    for (struct case_item *item = node->case_clause.items; item; item = item->next) {
        int matched = fallthrough;
        for (struct word *pattern = item->patterns; pattern && !matched; pattern = pattern->next) {
            struct arena_mark mark = arena_mark(stack);
            matched = !fnmatch(expand_pattern(pattern->text), subject, 0);
            arena_release(stack, mark);
        }
        if (!matched) continue;
        status = item->body ? evaluate(item->body, flags) : 0;
        if (skipping() || item->terminator == CASE_BREAK) break;
        fallthrough = item->terminator == CASE_FALLTHROUGH;
    }
    return status;
}

static int arith_command(struct node *node, int flags) {
    (void)flags;
    struct arena_mark mark = arena_mark(stack);
    char *expanded = expand_string(node->arith.expression, EXPAND_NOSPLIT);
    if (option_enabled(OPTION_XTRACE)) {
        char *argv[3] = { "((", expanded, "))" };
        xtrace(argv, 3, 0);
    }
    int status = arith_evaluate(expanded) ? 0 : 1;
    arena_release(stack, mark);
    return status;
}

static int cond_evaluate(struct cond *cond) {
    switch (cond->type) {
    case COND_NOT: return !cond_evaluate(cond->first);
    case COND_AND: return cond_evaluate(cond->first) && cond_evaluate(cond->second);
    case COND_OR: return cond_evaluate(cond->first) || cond_evaluate(cond->second);
    case COND_WORD: return *expand_string(cond->left->text, EXPAND_TILDE) != 0;
    case COND_UNARY: return test_unary(cond->op, expand_string(cond->left->text, EXPAND_TILDE));
    case COND_BINARY: {
        char *left = expand_string(cond->left->text, EXPAND_TILDE);
        if (!strcmp(cond->op, "==") || !strcmp(cond->op, "=") || !strcmp(cond->op, "!=")) {
            int matched = !fnmatch(expand_pattern(cond->right->text), left, 0);
            return cond->op[0] == '!' ? !matched : matched;
        }
        if (!strcmp(cond->op, "=~")) {
            char *right = expand_string(cond->right->text, EXPAND_TILDE);
            struct strbuf pattern;
            strbuf_init(&pattern, stack);
            strbuf_putc(&pattern, '*');
            strbuf_puts(&pattern, right);
            strbuf_putc(&pattern, '*');
            return !fnmatch(strbuf_string(&pattern), left, 0);
        }
        char *right = expand_string(cond->right->text, EXPAND_TILDE);
        int error = 0;
        int result = test_binary(left, cond->op, right, &error);
        if (error) sh_error("%s: conditional binary operator expected", cond->op);
        return result;
    }
    }
    return 0;
}

static int cond_command(struct node *node, int flags) {
    (void)flags;
    struct arena_mark mark = arena_mark(stack);
    int status = cond_evaluate(node->cond.expression) ? 0 : 1;
    arena_release(stack, mark);
    return status;
}

int evaluate(struct node *node, int flags) {
    if (!node) {
        shell.status = 0;
        if (flags & EVAL_EXIT) shell_exit(0);
        return 0;
    }
    check_interrupt();
    if (option_enabled(OPTION_NOEXEC) && !shell.interactive) return 0;
    shell.line = node->line;
    int status = 0;
    switch (node->type) {
    case NODE_SIMPLE:
        status = eval_simple(node, flags);
        shell.status = status;
        errexit(status, flags);
        break;
    case NODE_PIPELINE:
        status = eval_pipeline(node, flags);
        shell.status = status;
        errexit(status, flags);
        break;
    case NODE_AND:
    case NODE_OR:
        status = evaluate(node->binary.left, (flags | EVAL_TESTED) & ~EVAL_EXIT);
        if (!skipping() && (node->type == NODE_AND ? !status : status))
            status = evaluate(node->binary.right, flags);
        break;
    case NODE_SEQUENCE:
        evaluate(node->binary.left, flags & ~EVAL_EXIT);
        status = skipping() ? shell.status : evaluate(node->binary.right, flags);
        break;
    case NODE_BACKGROUND:
        status = eval_background(node, flags);
        break;
    case NODE_NOT:
        status = !evaluate(node->unary.body, (flags | EVAL_TESTED) & ~EVAL_EXIT);
        break;
    case NODE_SUBSHELL:
        status = eval_subshell(node, flags);
        shell.status = status;
        errexit(status, flags);
        break;
    case NODE_GROUP:
        status = with_redirects(node, flags, group_body);
        break;
    case NODE_IF:
        status = with_redirects(node, flags, if_body);
        break;
    case NODE_WHILE:
    case NODE_UNTIL:
        status = with_redirects(node, flags, loop_body);
        break;
    case NODE_FOR:
        status = with_redirects(node, flags, for_body);
        break;
    case NODE_ARITH_FOR:
        status = with_redirects(node, flags, arith_for_body);
        break;
    case NODE_CASE:
        status = with_redirects(node, flags, case_body);
        break;
    case NODE_ARITH:
        status = with_redirects(node, flags, arith_command);
        shell.status = status;
        errexit(status, flags);
        break;
    case NODE_COND:
        status = with_redirects(node, flags, cond_command);
        shell.status = status;
        errexit(status, flags);
        break;
    case NODE_FUNCTION:
        function_define(node);
        status = 0;
        break;
    }
    shell.status = status;
    if (trap_pending()) trap_run_pending();
    if (flags & EVAL_EXIT) shell_exit(shell.status);
    return status;
}

int evaluate_string(const char *text, const char *name, int flags) {
    struct arena_mark mark = arena_mark(stack);
    struct node *program = parse_string(stack, text, name);
    int status = evaluate(program, flags);
    arena_release(stack, mark);
    return status;
}

char *command_substitute(const char *text) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        sh_perror("pipe");
        return ststrdup("");
    }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        sh_perror("fork");
        close(fds[0]);
        close(fds[1]);
        return ststrdup("");
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        close(fds[1]);
        shell.subshell = 1;
        shell.interactive = 0;
        jobs_forget();
        jobs_disable();
        traps_reset_subshell();
        signals_reset_child(0);
        struct handler frame;
        int kind = TRY(&frame);
        if (!kind) evaluate_string(text, 0, EVAL_EXIT);
        END_TRY(&frame);
        shell_exit(kind == EXCEPTION_EXIT ? shell.status : kind ? exception_status : shell.status);
    }
    close(fds[1]);
    struct strbuf output;
    strbuf_init(&output, stack);
    char buffer[512];
    for (;;) {
        ssize_t got = read(fds[0], buffer, sizeof(buffer));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        strbuf_append(&output, buffer, (size_t)got);
    }
    close(fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    substitution_status = wait_status_code(status);
    shell.status = substitution_status;
    char *result = strbuf_string(&output);
    size_t length = output.length;
    while (length && result[length - 1] == '\n') length--;
    result[length] = 0;
    return result;
}
