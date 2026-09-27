#pragma once

#include <stdarg.h>
#include <stdio.h>

#define FILE_READ  0x01u
#define FILE_WRITE 0x02u
#define FILE_EOF   0x04u
#define FILE_ERROR 0x08u

struct __libc_file {
    int descriptor;
    unsigned int flags;
};

int __stdio_format(FILE *stream, const char *format, va_list arguments);
