#include <locale.h>
#include <string.h>

static char c_locale[] = "C";

char *setlocale(int category, const char *locale) {
    if (category < LC_CTYPE || category > LC_ALL) return 0;
    if (!locale || !*locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX")) return c_locale;
    return 0;
}

struct lconv *localeconv(void) {
    static struct lconv conventions = { ".", "", "" };
    return &conventions;
}
