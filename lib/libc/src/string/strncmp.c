#include <string.h>

int strncmp(const char *left, const char *right, size_t length) {
    for (; length; length--, left++, right++) {
        if (*left != *right) return (unsigned char)*left - (unsigned char)*right;
        if (!*left) return 0;
    }
    return 0;
}

int strcoll(const char *left, const char *right) {
    return strcmp(left, right);
}
