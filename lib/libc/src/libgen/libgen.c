#include <libgen.h>
#include <string.h>

char *basename(char *path) {
    static char dot[] = ".";
    if (!path || !*path) return dot;
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') path[--end] = 0;
    char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

char *dirname(char *path) {
    static char dot[] = ".";
    if (!path || !*path) return dot;
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') end--;
    while (end > 0 && path[end - 1] != '/') end--;
    if (!end) return dot;
    while (end > 1 && path[end - 1] == '/') end--;
    path[end] = 0;
    return path;
}
