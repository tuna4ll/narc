#include <ctype.h>
#include <fnmatch.h>
#include <string.h>

static int fold(int c, int flags) {
    return (flags & FNM_CASEFOLD) ? tolower(c) : c;
}

static int class_match(const char *name, size_t length, int c) {
    static const struct { const char *name; int (*test)(int); } classes[] = {
        { "alnum", isalnum }, { "alpha", isalpha }, { "blank", isblank },
        { "cntrl", iscntrl }, { "digit", isdigit }, { "graph", isgraph },
        { "lower", islower }, { "print", isprint }, { "punct", ispunct },
        { "space", isspace }, { "upper", isupper }, { "xdigit", isxdigit },
    };
    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++)
        if (strlen(classes[i].name) == length && !memcmp(classes[i].name, name, length))
            return classes[i].test(c) != 0;
    return 0;
}

static int bracket(const char **pattern, int c, int flags) {
    const char *p = *pattern + 1;
    int negate = *p == '!' || *p == '^';
    if (negate) p++;
    int matched = 0, first = 1, original = c;
    c = fold(c, flags);
    while (*p && (first || *p != ']')) {
        first = 0;
        if (p[0] == '[' && p[1] == ':') {
            const char *end = strstr(p + 2, ":]");
            if (end) {
                matched |= class_match(p + 2, (size_t)(end - p - 2), original) ||
                           class_match(p + 2, (size_t)(end - p - 2), c);
                p = end + 2;
                continue;
            }
        }
        int low = (unsigned char)*p;
        if (low == '\\' && !(flags & FNM_NOESCAPE) && p[1]) low = (unsigned char)*++p;
        p++;
        int high = low;
        if (p[0] == '-' && p[1] && p[1] != ']') {
            high = (unsigned char)p[1];
            p += 2;
            if (high == '\\' && !(flags & FNM_NOESCAPE) && *p) high = (unsigned char)*p++;
        }
        if (fold(low, flags) <= c && c <= fold(high, flags)) matched = 1;
    }
    if (*p != ']') return -1;
    *pattern = p + 1;
    return matched != negate;
}

static int leading_period(const char *string, const char *start, int flags) {
    return (flags & FNM_PERIOD) && *string == '.' &&
           (string == start || ((flags & FNM_PATHNAME) && string[-1] == '/'));
}

static int match(const char *pattern, const char *string, const char *start, int flags) {
    for (;;) {
        char c = *pattern;
        if (!c) return !*string || ((flags & FNM_LEADING_DIR) && *string == '/');
        if (c == '*') {
            while (*pattern == '*') pattern++;
            if (leading_period(string, start, flags)) return 0;
            for (;; string++) {
                if (match(pattern, string, start, flags)) return 1;
                if (!*string || ((flags & FNM_PATHNAME) && *string == '/')) return 0;
            }
        }
        if (!*string) return 0;
        if ((flags & FNM_PATHNAME) && *string == '/' && c != '/') return 0;
        if (c == '?') {
            if (leading_period(string, start, flags)) return 0;
            pattern++;
            string++;
            continue;
        }
        if (c == '[') {
            if (leading_period(string, start, flags)) return 0;
            const char *next = pattern;
            int result = bracket(&next, (unsigned char)*string, flags);
            if (result >= 0) {
                if (!result) return 0;
                pattern = next;
                string++;
                continue;
            }
        }
        if (c == '\\' && !(flags & FNM_NOESCAPE) && pattern[1]) c = *++pattern;
        if (fold((unsigned char)c, flags) != fold((unsigned char)*string, flags)) return 0;
        pattern++;
        string++;
    }
}

int fnmatch(const char *pattern, const char *string, int flags) {
    return match(pattern, string, string, flags) ? 0 : FNM_NOMATCH;
}
