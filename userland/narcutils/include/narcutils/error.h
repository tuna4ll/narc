#pragma once

#include <stdnoreturn.h>

extern const char *program_name;

void set_program_name(const char *argv0);
void error(int status, int errnum, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
noreturn void die(int status, int errnum, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
void close_stdout(void);
void initialize_main(int *argc, char ***argv);
