#include <ctype.h>
#include <strings.h>

int strncasecmp(const char *left, const char *right, size_t length) {
    for (; length; length--, left++, right++) {
        int a = tolower((unsigned char)*left), b = tolower((unsigned char)*right);
        if (a != b) return a - b;
        if (!a) return 0;
    }
    return 0;
}

int strcasecmp(const char *left, const char *right) {
    return strncasecmp(left, right, (size_t)-1);
}
