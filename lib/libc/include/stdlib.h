#pragma once

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;

_Noreturn void abort(void);
_Noreturn void exit(int status);
_Noreturn void _Exit(int status);
int atexit(void (*function)(void));
void *malloc(size_t size);
void free(void *pointer);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
long strtol(const char *restrict string, char **restrict end, int base);
long long strtoll(const char *restrict string, char **restrict end, int base);
unsigned long strtoul(const char *restrict string, char **restrict end, int base);
unsigned long long strtoull(const char *restrict string, char **restrict end, int base);
int atoi(const char *string);
long atol(const char *string);
long long atoll(const char *string);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *entry);
int clearenv(void);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
int abs(int value);
long labs(long value);
long long llabs(long long value);
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
int rand(void);
void srand(unsigned int seed);
double strtod(const char *restrict string, char **restrict end);
float strtof(const char *restrict string, char **restrict end);
long double strtold(const char *restrict string, char **restrict end);
double atof(const char *string);
int mkstemp(char *pattern);
int mkostemp(char *pattern, int flags);
char *mkdtemp(char *pattern);
char *realpath(const char *restrict path, char *restrict resolved);
