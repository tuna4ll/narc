#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

typedef struct __libc_file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fileno(FILE *stream);
int fflush(FILE *stream);
int fputc(int character, FILE *stream);
int putchar(int character);
size_t fwrite(const void *pointer, size_t size, size_t count, FILE *stream);
int fputs(const char *string, FILE *stream);
int puts(const char *string);
int vfprintf(FILE *stream, const char *format, va_list arguments);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list arguments);
int printf(const char *format, ...);
int rename(const char *old_path, const char *new_path);
