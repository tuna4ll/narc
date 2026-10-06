#pragma once

#include <stddef.h>

char *pablo_trim(char *text);
char *pablo_duplicate(const char *source, size_t length);
int pablo_has_suffix(const char *text, const char *suffix);
int pablo_parse_unsigned(const char *text, unsigned int *value);
