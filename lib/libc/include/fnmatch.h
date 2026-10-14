#pragma once

#define FNM_NOMATCH 1

#define FNM_NOESCAPE    0x01
#define FNM_PATHNAME    0x02
#define FNM_PERIOD      0x04
#define FNM_LEADING_DIR 0x08
#define FNM_CASEFOLD    0x10

int fnmatch(const char *pattern, const char *string, int flags);
