#include <stdlib.h>
#include <string.h>

char *strdup(const char *string) {
    size_t length = strlen(string) + 1;
    char *copy = malloc(length);
    return copy ? memcpy(copy, string, length) : 0;
}

char *strndup(const char *string, size_t length) {
    length = strnlen(string, length);
    char *copy = malloc(length + 1);
    if (!copy) return 0;
    memcpy(copy, string, length);
    copy[length] = 0;
    return copy;
}
