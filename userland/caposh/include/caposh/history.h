#pragma once

#include <stddef.h>

void history_init(void);
void history_add(const char *line);
size_t history_count(void);
size_t history_base(void);
const char *history_get(size_t index);
void history_clear(void);
void history_delete(size_t index);
int history_load(const char *path);
int history_save(const char *path);
char *history_expand(const char *line, int *changed);
