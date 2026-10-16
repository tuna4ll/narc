#include <caposh/error.h>
#include <caposh/input.h>
#include <caposh/shell.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct handler *handler;
int exception_status;

_Noreturn void raise_exception(enum exception kind) {
    if (!handler) shell_exit(kind == EXCEPTION_EXIT ? shell.status : exception_status);
    longjmp(handler->env, kind);
}

static void prefix(int parse_line) {
    fflush(stdout);
    if (shell.session_interactive && input_depth() <= 1 && input_interactive()) {
        fprintf(stderr, "%s: ", shell.name);
        return;
    }
    const char *source = input_name();
    fprintf(stderr, "%s: line %d: ", source ? source : shell.name,
            parse_line ? input_line() : shell.line);
}

static void report(const char *format, va_list arguments) {
    prefix(0);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
}

void sh_vwarning(const char *format, va_list arguments) {
    report(format, arguments);
}

void builtin_error(const char *name, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    prefix(0);
    fprintf(stderr, "%s: ", name);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
}

void sh_warning(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    report(format, arguments);
    va_end(arguments);
}

_Noreturn void sh_error(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    report(format, arguments);
    va_end(arguments);
    exception_status = 1;
    raise_exception(EXCEPTION_ERROR);
}

_Noreturn void sh_syntax_error(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    prefix(1);
    fputs("syntax error: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    exception_status = 2;
    raise_exception(EXCEPTION_SYNTAX);
}

void sh_perror(const char *subject) {
    int saved = errno;
    prefix(0);
    fprintf(stderr, "%s: %s\n", subject, strerror(saved));
}
