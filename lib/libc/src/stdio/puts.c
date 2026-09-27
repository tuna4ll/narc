#include <stdio.h>

int puts(const char *string) {
    if (fputs(string, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}
