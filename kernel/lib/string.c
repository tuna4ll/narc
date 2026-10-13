#include <kernel/string.h>
#include <stdint.h>

#define NO_BUILTIN __attribute__((optimize("no-tree-loop-distribute-patterns")))
#define WORD sizeof(uint64_t)

NO_BUILTIN void *memset(void *dst, int c, size_t n) {
    unsigned char *p = dst;
    while (n && ((uintptr_t)p & (WORD - 1))) {
        *p++ = (unsigned char)c;
        n--;
    }
    uint64_t pattern = 0x0101010101010101ULL * (unsigned char)c;
    for (; n >= WORD; n -= WORD, p += WORD) *(uint64_t *)p = pattern;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

NO_BUILTIN void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if ((((uintptr_t)d ^ (uintptr_t)s) & (WORD - 1)) == 0) {
        while (n && ((uintptr_t)d & (WORD - 1))) {
            *d++ = *s++;
            n--;
        }
        for (; n >= WORD; n -= WORD, d += WORD, s += WORD)
            *(uint64_t *)d = *(const uint64_t *)s;
    }
    while (n--) *d++ = *s++;
    return dst;
}

NO_BUILTIN void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d <= s || d >= s + n) return memcpy(dst, src, n);
    d += n;
    s += n;
    if ((((uintptr_t)d ^ (uintptr_t)s) & (WORD - 1)) == 0) {
        while (n && ((uintptr_t)d & (WORD - 1))) {
            *--d = *--s;
            n--;
        }
        for (; n >= WORD; n -= WORD) {
            d -= WORD;
            s -= WORD;
            *(uint64_t *)d = *(const uint64_t *)s;
        }
    }
    while (n--) *--d = *--s;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
