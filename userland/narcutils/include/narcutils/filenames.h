#pragma once

#include <stdbool.h>
#include <stddef.h>

char *last_component(const char *name);
size_t base_len(const char *name);
size_t dir_len(const char *name);
char *base_name(const char *name);
char *dir_name(const char *name);
bool strip_trailing_slashes(char *name);
