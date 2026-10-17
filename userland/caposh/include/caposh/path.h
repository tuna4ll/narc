#pragma once

char *path_search(const char *name, int use_hash);
void path_hash_clear(void);
void path_hash_remove(const char *name);
int path_hash_add(const char *name);
void path_hash_print(void);
int path_executable(const char *path);
