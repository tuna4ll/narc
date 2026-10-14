#include <string.h>

char *stpcpy(char *restrict dst, const char *restrict src) {
    while ((*dst = *src++)) dst++;
    return dst;
}

char *strcat(char *restrict dst, const char *restrict src) {
    stpcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *restrict dst, const char *restrict src, size_t length) {
    char *end = dst + strlen(dst);
    while (length-- && *src) *end++ = *src++;
    *end = 0;
    return dst;
}
