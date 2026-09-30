#include <errno.h>
#include <pablo/text.h>
#include <stdint.h>
#include <string.h>

static int space(char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n';
}

char *pablo_trim(char *text) {
    while (space(*text)) text++;
    size_t length = strlen(text);
    while (length && space(text[length - 1])) text[--length] = 0;
    return text;
}

int pablo_copy(char *destination, size_t capacity, const char *source) {
    size_t length = strlen(source);
    if (length >= capacity) return -1;
    memcpy(destination, source, length + 1);
    return 0;
}

int pablo_has_suffix(const char *text, const char *suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    if (suffix_length > text_length) return 0;
    return strcmp(text + text_length - suffix_length, suffix) == 0;
}

int pablo_parse_unsigned(const char *text, unsigned int *value) {
    if (!*text) return -1;
    unsigned int result = 0;
    while (*text) {
        if (*text < '0' || *text > '9') return -1;
        unsigned int digit = (unsigned int)(*text - '0');
        if (result > (UINT32_MAX - digit) / 10) return -1;
        result = result * 10 + digit;
        text++;
    }
    *value = result;
    return 0;
}
