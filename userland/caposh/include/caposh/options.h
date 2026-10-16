#pragma once

enum option {
    OPTION_ALLEXPORT,
    OPTION_NOTIFY,
    OPTION_ERREXIT,
    OPTION_NOGLOB,
    OPTION_HASHALL,
    OPTION_MONITOR,
    OPTION_NOEXEC,
    OPTION_NOUNSET,
    OPTION_VERBOSE,
    OPTION_XTRACE,
    OPTION_NOCLOBBER,
    OPTION_IGNOREEOF,
    OPTION_PIPEFAIL,
    OPTION_EMACS,
    OPTION_INTERACTIVE_COMMENTS,
    OPTION_NOLOG,
    OPTION_COUNT,
};

int option_enabled(enum option option);
void option_set(enum option option, int enabled);
int option_by_letter(char letter);
int option_by_name(const char *name);
const char *option_name(enum option option);
char option_letter(enum option option);
char *option_flags(void);
void option_print(int reusable);
