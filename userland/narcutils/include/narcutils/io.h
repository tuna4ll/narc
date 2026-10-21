#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define IO_BUFSIZE (128 * 1024)
#define SAFE_READ_ERROR ((size_t)-1)

size_t safe_read(int fd, void *buffer, size_t count);
size_t safe_write(int fd, const void *buffer, size_t count);
size_t full_read(int fd, void *buffer, size_t count);
size_t full_write(int fd, const void *buffer, size_t count);
int xopen_input(const char *name, bool *is_stdin);
int close_input(int fd, bool is_stdin);
void xset_binary_mode(int fd);
