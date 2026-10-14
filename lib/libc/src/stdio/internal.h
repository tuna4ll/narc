#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>

#define FILE_READ     0x01u
#define FILE_WRITE    0x02u
#define FILE_EOF      0x04u
#define FILE_ERROR    0x08u
#define FILE_STATIC   0x10u
#define FILE_OWNED    0x20u
#define FILE_DETECT   0x40u

enum stream_direction { STREAM_IDLE, STREAM_READING, STREAM_WRITING };

struct __libc_file {
    int descriptor;
    unsigned int flags;
    int mode;
    unsigned char *buffer;
    size_t size;
    size_t read_pos, read_end;
    size_t write_pos;
    enum stream_direction direction;
    struct __libc_file *next;
};

struct format_sink {
    void (*put)(struct format_sink *sink, const char *data, size_t length);
    void *context;
    size_t total;
    int failed;
};

int __stdio_format(struct format_sink *sink, const char *format, va_list arguments);
int __stdio_prepare_read(FILE *stream);
int __stdio_prepare_write(FILE *stream);
int __stdio_flush_write(FILE *stream);
int __stdio_fill(FILE *stream);
void __stdio_register(FILE *stream);
void __stdio_unregister(FILE *stream);
int __stdio_mode_flags(const char *mode, int *flags);
