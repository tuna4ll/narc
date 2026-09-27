#include <stdio.h>
#include <string.h>

int fputs(const char *string, FILE *stream) {
    size_t length = strlen(string);
    return fwrite(string, 1, length, stream) == length ? 0 : EOF;
}
