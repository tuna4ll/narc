#include <stdarg.h>
#include <stdio.h>

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}
