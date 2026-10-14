#include <string.h>

size_t strspn(const char *string, const char *accept) {
    size_t length = 0;
    while (string[length] && strchr(accept, string[length])) length++;
    return length;
}

size_t strcspn(const char *string, const char *reject) {
    size_t length = 0;
    while (string[length] && !strchr(reject, string[length])) length++;
    return length;
}

char *strpbrk(const char *string, const char *accept) {
    string += strcspn(string, accept);
    return *string ? (char *)string : 0;
}
