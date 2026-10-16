#pragma once

#include <setjmp.h>
#include <stdarg.h>

enum exception {
    EXCEPTION_NONE,
    EXCEPTION_ERROR,
    EXCEPTION_SYNTAX,
    EXCEPTION_EXIT,
    EXCEPTION_INTERRUPT,
};

struct handler {
    jmp_buf env;
    struct handler *previous;
};

extern struct handler *handler;
extern int exception_status;

#define TRY(frame) (((frame)->previous = handler), (handler = (frame)), setjmp((frame)->env))
#define END_TRY(frame) (handler = (frame)->previous)

_Noreturn void raise_exception(enum exception kind);
_Noreturn void sh_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
_Noreturn void sh_syntax_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
void sh_warning(const char *format, ...) __attribute__((format(printf, 1, 2)));
void sh_vwarning(const char *format, va_list arguments);
void builtin_error(const char *name, const char *format, ...) __attribute__((format(printf, 2, 3)));
void sh_perror(const char *subject);
