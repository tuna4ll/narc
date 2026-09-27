#include "internal.h"

int vfprintf(FILE *stream, const char *format, va_list arguments) {
    return __stdio_format(stream, format, arguments);
}
