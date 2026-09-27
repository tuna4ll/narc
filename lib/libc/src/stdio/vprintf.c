#include <stdio.h>

int vprintf(const char *format, va_list arguments) {
    return vfprintf(stdout, format, arguments);
}
