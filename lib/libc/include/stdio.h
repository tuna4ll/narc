#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define EOF (-1)
#define BUFSIZ 4096
#define FILENAME_MAX 4096

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

typedef struct __libc_file FILE;
typedef off_t fpos_t;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *restrict path, const char *restrict mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *restrict path, const char *restrict mode, FILE *restrict stream);
int fclose(FILE *stream);
int fflush(FILE *stream);
int setvbuf(FILE *restrict stream, char *restrict buffer, int mode, size_t size);
void setbuf(FILE *restrict stream, char *restrict buffer);
void setlinebuf(FILE *stream);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fileno(FILE *stream);

int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
int ungetc(int character, FILE *stream);
char *fgets(char *restrict buffer, int size, FILE *restrict stream);
size_t fread(void *restrict pointer, size_t size, size_t count, FILE *restrict stream);
ssize_t getline(char **restrict line, size_t *restrict capacity, FILE *restrict stream);
ssize_t getdelim(char **restrict line, size_t *restrict capacity, int delimiter,
                 FILE *restrict stream);

int fputc(int character, FILE *stream);
int putc(int character, FILE *stream);
int putchar(int character);
int fputs(const char *restrict string, FILE *restrict stream);
int puts(const char *string);
size_t fwrite(const void *restrict pointer, size_t size, size_t count, FILE *restrict stream);

int fseek(FILE *stream, long offset, int origin);
int fseeko(FILE *stream, off_t offset, int origin);
long ftell(FILE *stream);
off_t ftello(FILE *stream);
void rewind(FILE *stream);

int printf(const char *restrict format, ...);
int fprintf(FILE *restrict stream, const char *restrict format, ...);
int dprintf(int fd, const char *restrict format, ...);
int sprintf(char *restrict buffer, const char *restrict format, ...);
int snprintf(char *restrict buffer, size_t size, const char *restrict format, ...);
int asprintf(char **result, const char *restrict format, ...);
int vprintf(const char *restrict format, va_list arguments);
int vfprintf(FILE *restrict stream, const char *restrict format, va_list arguments);
int vdprintf(int fd, const char *restrict format, va_list arguments);
int vsprintf(char *restrict buffer, const char *restrict format, va_list arguments);
int vsnprintf(char *restrict buffer, size_t size, const char *restrict format,
              va_list arguments);
int vasprintf(char **result, const char *restrict format, va_list arguments);

void perror(const char *prefix);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
