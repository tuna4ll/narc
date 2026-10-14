#include <string.h>

char *strstr(const char *haystack, const char *needle) {
    size_t length = strlen(needle);
    if (!length) return (char *)haystack;
    for (; *haystack; haystack++)
        if (*haystack == *needle && !strncmp(haystack, needle, length)) return (char *)haystack;
    return 0;
}
