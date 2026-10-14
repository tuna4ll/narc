#include <string.h>

char *strrchr(const char *string, int character) {
    const char *found = 0;
    for (;; string++) {
        if (*string == (char)character) found = string;
        if (!*string) return (char *)found;
    }
}

char *strchrnul(const char *string, int character) {
    while (*string && *string != (char)character) string++;
    return (char *)string;
}
