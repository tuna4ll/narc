#include <stdio.h>

int fputc(int character, FILE *stream) {
    unsigned char byte = (unsigned char)character;
    return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}
