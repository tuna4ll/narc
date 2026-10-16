#include <caposh/memory.h>
#include <caposh/options.h>
#include <caposh/shell.h>
#include <stdio.h>
#include <string.h>

static int enabled[OPTION_COUNT] = {
    [OPTION_HASHALL] = 1,
    [OPTION_EMACS] = 1,
    [OPTION_INTERACTIVE_COMMENTS] = 1,
};

static const struct {
    const char *name;
    char letter;
} table[OPTION_COUNT] = {
    [OPTION_ALLEXPORT] = { "allexport", 'a' },
    [OPTION_NOTIFY] = { "notify", 'b' },
    [OPTION_ERREXIT] = { "errexit", 'e' },
    [OPTION_NOGLOB] = { "noglob", 'f' },
    [OPTION_HASHALL] = { "hashall", 'h' },
    [OPTION_MONITOR] = { "monitor", 'm' },
    [OPTION_NOEXEC] = { "noexec", 'n' },
    [OPTION_NOUNSET] = { "nounset", 'u' },
    [OPTION_VERBOSE] = { "verbose", 'v' },
    [OPTION_XTRACE] = { "xtrace", 'x' },
    [OPTION_NOCLOBBER] = { "noclobber", 'C' },
    [OPTION_IGNOREEOF] = { "ignoreeof", 0 },
    [OPTION_PIPEFAIL] = { "pipefail", 0 },
    [OPTION_EMACS] = { "emacs", 0 },
    [OPTION_INTERACTIVE_COMMENTS] = { "interactive-comments", 0 },
    [OPTION_NOLOG] = { "nolog", 0 },
};

int option_enabled(enum option option) {
    return enabled[option];
}

void option_set(enum option option, int value) {
    enabled[option] = value != 0;
}

int option_by_letter(char letter) {
    for (int i = 0; i < OPTION_COUNT; i++)
        if (table[i].letter == letter) return i;
    return -1;
}

int option_by_name(const char *name) {
    for (int i = 0; i < OPTION_COUNT; i++)
        if (!strcmp(table[i].name, name)) return i;
    return -1;
}

const char *option_name(enum option option) {
    return table[option].name;
}

char option_letter(enum option option) {
    return table[option].letter;
}

char *option_flags(void) {
    char buffer[OPTION_COUNT + 4];
    size_t length = 0;
    for (int i = 0; i < OPTION_COUNT; i++)
        if (enabled[i] && table[i].letter) buffer[length++] = table[i].letter;
    if (shell.interactive) buffer[length++] = 'i';
    buffer[length] = 0;
    return xstrdup(buffer);
}

void option_print(int reusable) {
    for (int i = 0; i < OPTION_COUNT; i++) {
        if (reusable) printf("set %co %s\n", enabled[i] ? '-' : '+', table[i].name);
        else printf("%-20s\t%s\n", table[i].name, enabled[i] ? "on" : "off");
    }
}
