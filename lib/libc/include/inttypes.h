#pragma once

#include <stdint.h>

#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "lu"
#define PRIx32 "x"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define PRIo64 "lo"
#define PRIdMAX "jd"
#define PRIuMAX "ju"
#define PRIxMAX "jx"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#define PRIxPTR "lx"
#define SCNd64 "ld"
#define SCNu64 "lu"

intmax_t strtoimax(const char *restrict string, char **restrict end, int base);
uintmax_t strtoumax(const char *restrict string, char **restrict end, int base);
intmax_t imaxabs(intmax_t value);
