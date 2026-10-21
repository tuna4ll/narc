#pragma once

#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#define CHMOD_MODE_BITS 07777
#define S_IRWXUGO 0777

struct mode_change;

struct mode_change *mode_compile(const char *mode_string);
struct mode_change *mode_create_from_ref(const char *reference_file);
mode_t mode_adjust(mode_t old_mode, bool directory, mode_t umask_value,
                   const struct mode_change *changes, mode_t *mode_bits);
void strmode(mode_t mode, char *string);
void filemodestring(const struct stat *st, char *string);
