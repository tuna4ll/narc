#pragma once

#include <stddef.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t length);
void *memmove(void *dst, const void *src, size_t length);
void *memset(void *dst, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
size_t strlen(const char *string);
size_t strnlen(const char *string, size_t limit);
int strcmp(const char *left, const char *right);
char *strcpy(char *restrict dst, const char *restrict src);
char *strncpy(char *restrict dst, const char *restrict src, size_t length);
char *strchr(const char *string, int character);
void *memchr(const void *memory, int character, size_t length);
void *memrchr(const void *memory, int character, size_t length);
int strncmp(const char *left, const char *right, size_t length);
int strcoll(const char *left, const char *right);
char *strcat(char *restrict dst, const char *restrict src);
char *strncat(char *restrict dst, const char *restrict src, size_t length);
char *stpcpy(char *restrict dst, const char *restrict src);
char *strrchr(const char *string, int character);
char *strchrnul(const char *string, int character);
char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *string, const char *accept);
size_t strspn(const char *string, const char *accept);
size_t strcspn(const char *string, const char *reject);
char *strdup(const char *string);
char *strndup(const char *string, size_t length);
char *strtok(char *restrict string, const char *restrict separators);
char *strtok_r(char *restrict string, const char *restrict separators, char **restrict state);
char *strerror(int error);
char *strsignal(int signal);
