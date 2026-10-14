#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

extern char **environ;
static char **owned;
static size_t owned_capacity;

static size_t name_length(const char *name) {
    return strcspn(name, "=");
}

static ssize_t find(const char *name, size_t length) {
    if (!environ) return -1;
    for (size_t i = 0; environ[i]; i++)
        if (!strncmp(environ[i], name, length) && environ[i][length] == '=') return (ssize_t)i;
    return -1;
}

char *getenv(const char *name) {
    size_t length = name_length(name);
    if (name[length]) return 0;
    ssize_t index = find(name, length);
    return index < 0 ? 0 : environ[index] + length + 1;
}

static size_t count(void) {
    size_t n = 0;
    while (environ && environ[n]) n++;
    return n;
}

static int adopt(size_t extra) {
    size_t n = count();
    if (environ == owned && n + extra + 1 <= owned_capacity) return 0;
    size_t capacity = (n + extra + 1) * 2;
    char **next = malloc(capacity * sizeof(char *));
    if (!next) {
        errno = ENOMEM;
        return -1;
    }
    for (size_t i = 0; i < n; i++) next[i] = environ[i];
    next[n] = 0;
    if (environ == owned) free(owned);
    environ = owned = next;
    owned_capacity = capacity;
    return 0;
}

int putenv(char *entry) {
    size_t length = name_length(entry);
    if (!entry[length]) return unsetenv(entry);
    if (adopt(1) != 0) return -1;
    ssize_t index = find(entry, length);
    if (index >= 0) {
        environ[index] = entry;
        return 0;
    }
    size_t n = count();
    environ[n] = entry;
    environ[n + 1] = 0;
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    size_t length = strlen(name);
    if (!length || name[name_length(name)]) {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && find(name, length) >= 0) return 0;
    size_t value_length = strlen(value);
    char *entry = malloc(length + value_length + 2);
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, length);
    entry[length] = '=';
    memcpy(entry + length + 1, value, value_length + 1);
    return putenv(entry);
}

int unsetenv(const char *name) {
    size_t length = strlen(name);
    if (!length || name[name_length(name)]) {
        errno = EINVAL;
        return -1;
    }
    ssize_t index;
    while ((index = find(name, length)) >= 0) {
        if (adopt(0) != 0) return -1;
        size_t i = (size_t)index;
        for (; environ[i]; i++) environ[i] = environ[i + 1];
    }
    return 0;
}

int clearenv(void) {
    if (environ == owned) free(owned);
    owned = 0;
    owned_capacity = 0;
    environ = 0;
    return 0;
}
