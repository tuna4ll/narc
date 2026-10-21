#include <narcutils/system.h>
#include <stdarg.h>

void xalloc_die(void) {
    error(EXIT_FAILURE, 0, "memory exhausted");
    abort();
}

void *xmalloc(size_t size) {
    void *pointer = malloc(size ? size : 1);
    if (!pointer) xalloc_die();
    return pointer;
}

void *xzalloc(size_t size) {
    return xcalloc(1, size);
}

void *xcalloc(size_t count, size_t size) {
    void *pointer = calloc(count ? count : 1, size ? size : 1);
    if (!pointer) xalloc_die();
    return pointer;
}

void *xrealloc(void *pointer, size_t size) {
    pointer = realloc(pointer, size ? size : 1);
    if (!pointer) xalloc_die();
    return pointer;
}

void *xnmalloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) xalloc_die();
    return xmalloc(count * size);
}

void *xnrealloc(void *pointer, size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) xalloc_die();
    return xrealloc(pointer, count * size);
}

void *x2nrealloc(void *pointer, size_t *count, size_t size) {
    size_t n = *count;
    if (!pointer) {
        if (!n) n = size >= 128 ? 1 : 128 / size;
    } else {
        if (n > SIZE_MAX / 3 * 2 / (size ? size : 1)) xalloc_die();
        n += n / 2 + 1;
    }
    *count = n;
    return xnrealloc(pointer, n, size);
}

char *xstrdup(const char *string) {
    return xmemdup(string, strlen(string) + 1);
}

char *xstrndup(const char *string, size_t length) {
    size_t actual = strnlen(string, length);
    char *copy = xmalloc(actual + 1);
    memcpy(copy, string, actual);
    copy[actual] = 0;
    return copy;
}

void *xmemdup(const void *source, size_t size) {
    return memcpy(xmalloc(size), source, size);
}

char *xasprintf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char *result;
    int length = vasprintf(&result, format, arguments);
    va_end(arguments);
    if (length < 0) xalloc_die();
    return result;
}

char *file_name_concat(const char *directory, const char *name) {
    size_t directory_length = strlen(directory);
    bool slash = directory_length && directory[directory_length - 1] == '/';
    while (*name == '/' && slash) name++;
    return xasprintf("%s%s%s", directory, slash || !directory_length ? "" : "/", name);
}
