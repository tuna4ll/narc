#include <string.h>

void *memchr(const void *memory, int character, size_t length) {
    const unsigned char *bytes = memory;
    for (size_t i = 0; i < length; i++)
        if (bytes[i] == (unsigned char)character) return (void *)(bytes + i);
    return 0;
}

void *memrchr(const void *memory, int character, size_t length) {
    const unsigned char *bytes = memory;
    while (length--)
        if (bytes[length] == (unsigned char)character) return (void *)(bytes + length);
    return 0;
}
