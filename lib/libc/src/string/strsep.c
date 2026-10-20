#include <string.h>

char *strsep(char **restrict string, const char *restrict separators) {
    char *start = *string;
    if (!start) return 0;
    char *end = start + strcspn(start, separators);
    if (*end) {
        *end = 0;
        *string = end + 1;
    } else {
        *string = 0;
    }
    return start;
}
