#include <narcutils/filenames.h>
#include <narcutils/system.h>

char *last_component(const char *name) {
    const char *base = name;
    bool last_was_slash = false;
    while (*base == '/') base++;
    for (const char *p = base; *p; p++) {
        if (*p == '/') {
            last_was_slash = true;
        } else if (last_was_slash) {
            base = p;
            last_was_slash = false;
        }
    }
    return (char *)base;
}

size_t base_len(const char *name) {
    size_t length = strlen(name);
    while (length > 1 && name[length - 1] == '/') length--;
    return length;
}

size_t dir_len(const char *name) {
    size_t prefix = name[0] == '/';
    size_t length = (size_t)(last_component(name) - name);
    while (prefix < length && name[length - 1] == '/') length--;
    return length;
}

char *base_name(const char *name) {
    const char *base = last_component(name);
    if (!*base) return xstrndup(name, base_len(name));
    return xstrndup(base, base_len(base));
}

char *dir_name(const char *name) {
    size_t length = dir_len(name);
    if (!length) return xstrdup(".");
    return xstrndup(name, length);
}

bool strip_trailing_slashes(char *name) {
    char *base = last_component(name);
    if (!*base) base = name;
    char *end = base + base_len(base);
    bool stripped = *end != 0;
    *end = 0;
    return stripped;
}
