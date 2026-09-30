#pragma once

#include <stddef.h>

char *pablo_trim(char *text);
int pablo_copy(char *destination, size_t capacity, const char *source);
int pablo_has_suffix(const char *text, const char *suffix);
int pablo_parse_unsigned(const char *text, unsigned int *value);
