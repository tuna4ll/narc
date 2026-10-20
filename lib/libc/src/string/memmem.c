#include <strings.h>
#include <string.h>

void *memmem(const void *haystack, size_t haystack_length, const void *needle,
             size_t needle_length) {
    const unsigned char *h = haystack, *n = needle;
    if (!needle_length) return (void *)haystack;
    if (needle_length > haystack_length) return 0;
    for (size_t i = 0; i + needle_length <= haystack_length; i++)
        if (h[i] == n[0] && !memcmp(h + i, n, needle_length)) return (void *)(h + i);
    return 0;
}

void *mempcpy(void *restrict dst, const void *restrict src, size_t length) {
    return (char *)memcpy(dst, src, length) + length;
}

char *strcasestr(const char *haystack, const char *needle) {
    size_t length = strlen(needle);
    for (; *haystack; haystack++)
        if (!strncasecmp(haystack, needle, length)) return (char *)haystack;
    return length ? 0 : (char *)haystack;
}
