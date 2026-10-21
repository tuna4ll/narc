#pragma once

#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

enum {
    GETOPT_HELP_CHAR = CHAR_MIN - 2,
    GETOPT_VERSION_CHAR = CHAR_MIN - 3,
};

#define GETOPT_HELP_OPTION_DECL { "help", no_argument, 0, GETOPT_HELP_CHAR }
#define GETOPT_VERSION_OPTION_DECL { "version", no_argument, 0, GETOPT_VERSION_CHAR }

#define case_GETOPT_HELP_CHAR       \
    case GETOPT_HELP_CHAR:          \
        usage(EXIT_SUCCESS);        \
        break

#define case_GETOPT_VERSION_CHAR(name) \
    case GETOPT_VERSION_CHAR:          \
        version_etc(name);             \
        exit(EXIT_SUCCESS)

#define HELP_OPTION_DESCRIPTION "      --help        display this help and exit\n"
#define VERSION_OPTION_DESCRIPTION "      --version     output version information and exit\n"

noreturn void usage(int status);
void version_etc(const char *command_name);
void emit_try_help(void);
void emit_stdin_note(void);
void emit_mandatory_arg_note(void);
void emit_size_suffixes(void);
void emit_ancillary_info(const char *command_name);
void parse_long_options_only(int argc, char **argv, const char *command_name, bool help);
void parse_gnu_standard_options_only(int argc, char **argv, const char *command_name);
int argmatch(const char *arg, const char *const *choices);
int xargmatch(const char *context, const char *arg, const char *const *choices);
int xargmatch_values(const char *context, const char *arg, const char *const *choices,
                     const void *values, size_t value_size);

#define XARGMATCH(context, arg, choices, values) \
    ((values)[xargmatch_values(context, arg, choices, values, sizeof *(values))])
