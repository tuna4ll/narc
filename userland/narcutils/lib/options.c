#include <narcutils/quote.h>
#include <narcutils/system.h>

void version_etc(const char *command_name) {
    printf("%s (narcutils) %s\n", command_name, NARCUTILS_VERSION);
    fputs("Part of the narc operating system.\n"
          "This is free software: you are free to change and redistribute it.\n",
          stdout);
}

void emit_try_help(void) {
    fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
}

void emit_stdin_note(void) {
    fputs("\nWith no FILE, or when FILE is -, read standard input.\n", stdout);
}

void emit_mandatory_arg_note(void) {
    fputs("\nMandatory arguments to long options are mandatory for short options too.\n", stdout);
}

void emit_size_suffixes(void) {
    fputs("\nThe SIZE argument is an integer and optional unit (example: 10K is 10*1024).\n"
          "Units are K,M,G,T,P,E,Z,Y,R,Q (powers of 1024) or KB,MB,... (powers of 1000).\n"
          "Binary prefixes can be used, too: KiB=K, MiB=M, and so on.\n",
          stdout);
}

void emit_ancillary_info(const char *command_name) {
    printf("\nnarcutils documentation: see /usr/share/doc/narcutils or '%s --help'\n", command_name);
}

static const struct option standard_options[] = {
    GETOPT_HELP_OPTION_DECL,
    GETOPT_VERSION_OPTION_DECL,
    { 0, 0, 0, 0 },
};

void parse_long_options_only(int argc, char **argv, const char *command_name, bool help) {
    int saved_opterr = opterr;
    opterr = 0;
    if (argc == 2) {
        int c = getopt_long(argc, argv, "+", standard_options, 0);
        if (c == GETOPT_HELP_CHAR && help) usage(EXIT_SUCCESS);
        if (c == GETOPT_VERSION_CHAR) {
            version_etc(command_name);
            exit(EXIT_SUCCESS);
        }
    }
    opterr = saved_opterr;
    optind = 0;
}

void parse_gnu_standard_options_only(int argc, char **argv, const char *command_name) {
    int c;
    while ((c = getopt_long(argc, argv, "+", standard_options, 0)) != -1) {
        switch (c) {
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(command_name);
        default:
            usage(EXIT_FAILURE);
        }
    }
}

static int argmatch_values(const char *arg, const char *const *choices, const void *values,
                           size_t value_size) {
    size_t length = strlen(arg);
    int found = -1;
    bool ambiguous = false;
    for (int i = 0; choices[i]; i++) {
        if (strncmp(choices[i], arg, length)) continue;
        if (strlen(choices[i]) == length) return i;
        if (found == -1) {
            found = i;
        } else if (!values || memcmp((const char *)values + (size_t)found * value_size,
                                     (const char *)values + (size_t)i * value_size, value_size)) {
            ambiguous = true;
        }
    }
    return ambiguous ? -2 : found;
}

int argmatch(const char *arg, const char *const *choices) {
    return argmatch_values(arg, choices, 0, 0);
}

int xargmatch_values(const char *context, const char *arg, const char *const *choices,
                     const void *values, size_t value_size) {
    int index = argmatch_values(arg, choices, values, value_size);
    if (index >= 0) return index;
    error(0, 0, index == -2 ? "ambiguous argument %s for %s" : "invalid argument %s for %s",
          quote_n(0, arg), quote_n(1, context));
    fputs("Valid arguments are:", stderr);
    const char *previous = 0;
    for (int i = 0; choices[i]; i++) {
        const char *value = values ? (const char *)values + (size_t)i * value_size : 0;
        if (i == 0 || !values || memcmp(previous, value, value_size)) {
            fprintf(stderr, "\n  - %s", quote(choices[i]));
            previous = value;
        } else {
            fprintf(stderr, ", %s", quote(choices[i]));
        }
    }
    fputc('\n', stderr);
    usage(EXIT_FAILURE);
}

int xargmatch(const char *context, const char *arg, const char *const *choices) {
    return xargmatch_values(context, arg, choices, 0, 0);
}
