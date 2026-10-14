#include <string.h>

char *strtok_r(char *restrict string, const char *restrict separators, char **restrict state) {
    if (!string) string = *state;
    if (!string) return 0;
    string += strspn(string, separators);
    if (!*string) {
        *state = 0;
        return 0;
    }
    char *end = string + strcspn(string, separators);
    if (*end) *end++ = 0;
    else end = 0;
    *state = end;
    return string;
}

char *strtok(char *restrict string, const char *restrict separators) {
    static char *state;
    return strtok_r(string, separators, &state);
}
