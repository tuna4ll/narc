#pragma once

int glob_expand(const char *pattern, char ***matches);
int glob_has_magic(const char *pattern);
char *glob_unescape(const char *pattern);
