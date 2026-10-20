#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ordering { PERMUTE, REQUIRE_ORDER, RETURN_IN_ORDER };

static char *nextchar;
static int first_nonopt, last_nonopt, started_at;

static int nonoption(const char *argument) {
    return argument[0] != '-' || !argument[1];
}

static void exchange(char **argv) {
    int bottom = first_nonopt, middle = last_nonopt, top = optind;
    while (top > middle && middle > bottom) {
        if (top - middle > middle - bottom) {
            int length = middle - bottom;
            for (int i = 0; i < length; i++) {
                char *swap = argv[bottom + i];
                argv[bottom + i] = argv[top - length + i];
                argv[top - length + i] = swap;
            }
            top -= length;
        } else {
            int length = top - middle;
            for (int i = 0; i < length; i++) {
                char *swap = argv[bottom + i];
                argv[bottom + i] = argv[middle + i];
                argv[middle + i] = swap;
            }
            bottom += length;
        }
    }
    first_nonopt += optind - last_nonopt;
    last_nonopt = optind;
}

static int long_option(int argc, char *const argv[], const char *prefix, char *name,
                       const struct option *longopts, int *longindex, int silent, int long_only,
                       const char *options) {
    char *equals = strchr(name, '=');
    size_t length = equals ? (size_t)(equals - name) : strlen(name);
    const struct option *found = 0;
    int index = -1, ambiguous = 0, exact = 0;
    for (int i = 0; longopts[i].name; i++) {
        const struct option *candidate = &longopts[i];
        if (strncmp(candidate->name, name, length)) continue;
        if (strlen(candidate->name) == length) {
            found = candidate;
            index = i;
            exact = 1;
            break;
        }
        if (!found) {
            found = candidate;
            index = i;
        } else if (found->has_arg != candidate->has_arg || found->flag != candidate->flag ||
                   found->val != candidate->val) {
            ambiguous = 1;
        }
    }
    if (ambiguous && !exact) {
        if (opterr && !silent) {
            fprintf(stderr, "%s: option '%s%.*s' is ambiguous; possibilities:", argv[0], prefix,
                    (int)length, name);
            for (int i = 0; longopts[i].name; i++)
                if (!strncmp(longopts[i].name, name, length))
                    fprintf(stderr, " '%s%s'", prefix, longopts[i].name);
            fputc('\n', stderr);
        }
        nextchar = 0;
        optind++;
        optopt = 0;
        return '?';
    }
    if (!found) {
        if (long_only && prefix[1] == 0 && strchr(options, *name)) return -2;
        if (opterr && !silent) fprintf(stderr, "%s: unrecognized option '%s%s'\n", argv[0], prefix, name);
        nextchar = 0;
        optind++;
        optopt = 0;
        return '?';
    }
    optind++;
    nextchar = 0;
    if (equals) {
        if (found->has_arg == no_argument) {
            if (opterr && !silent)
                fprintf(stderr, "%s: option '%s%s' doesn't allow an argument\n", argv[0], prefix,
                        found->name);
            optopt = found->val;
            return '?';
        }
        optarg = equals + 1;
    } else if (found->has_arg == required_argument) {
        if (optind < argc) {
            optarg = argv[optind++];
        } else {
            if (opterr && !silent)
                fprintf(stderr, "%s: option '%s%s' requires an argument\n", argv[0], prefix,
                        found->name);
            optopt = found->val;
            return silent ? ':' : '?';
        }
    }
    if (longindex) *longindex = index;
    if (found->flag) {
        *found->flag = found->val;
        return 0;
    }
    return found->val;
}

static int getopt_internal(int argc, char *const argv[], const char *options,
                           const struct option *longopts, int *longindex, int long_only) {
    char **args = (char **)argv;
    if (optind == 0 || !started_at) {
        if (optind == 0) optind = 1;
        first_nonopt = last_nonopt = optind;
        nextchar = 0;
        started_at = 1;
    }
    enum ordering ordering = PERMUTE;
    if (*options == '-') {
        ordering = RETURN_IN_ORDER;
        options++;
    } else if (*options == '+') {
        ordering = REQUIRE_ORDER;
        options++;
    } else if (getenv("POSIXLY_CORRECT")) {
        ordering = REQUIRE_ORDER;
    }
    int silent = *options == ':';
    optarg = 0;

    if (!nextchar || !*nextchar) {
        if (last_nonopt > optind) last_nonopt = optind;
        if (first_nonopt > optind) first_nonopt = optind;
        if (ordering == PERMUTE) {
            if (first_nonopt != last_nonopt && last_nonopt != optind) exchange(args);
            else if (last_nonopt != optind) first_nonopt = optind;
            while (optind < argc && nonoption(argv[optind])) optind++;
            last_nonopt = optind;
        }
        if (optind != argc && !strcmp(argv[optind], "--")) {
            optind++;
            if (first_nonopt != last_nonopt && last_nonopt != optind) exchange(args);
            else if (first_nonopt == last_nonopt) first_nonopt = optind;
            last_nonopt = argc;
            optind = argc;
        }
        if (optind == argc) {
            if (first_nonopt != last_nonopt) optind = first_nonopt;
            started_at = 0;
            return -1;
        }
        if (nonoption(argv[optind])) {
            if (ordering == REQUIRE_ORDER) {
                started_at = 0;
                return -1;
            }
            optarg = argv[optind++];
            return 1;
        }
        if (longopts && argv[optind][1] == '-') {
            return long_option(argc, argv, "--", argv[optind] + 2, longopts, longindex, silent, 0,
                               options);
        }
        if (longopts && long_only) {
            int result = long_option(argc, argv, "-", argv[optind] + 1, longopts, longindex,
                                     silent, 1, options);
            if (result != -2) return result;
        }
        nextchar = argv[optind] + 1;
    }

    char c = *nextchar++;
    const char *match = c != ':' ? strchr(options, c) : 0;
    if (!*nextchar) optind++;
    if (!match || !c) {
        if (opterr && !silent) fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
        optopt = c;
        return '?';
    }
    if (match[1] != ':') return c;
    if (match[2] == ':') {
        if (*nextchar) {
            optarg = nextchar;
            optind++;
        }
        nextchar = 0;
        return c;
    }
    if (*nextchar) {
        optarg = nextchar;
        optind++;
    } else if (optind < argc) {
        optarg = argv[optind++];
    } else {
        if (opterr && !silent)
            fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
        optopt = c;
        nextchar = 0;
        return silent ? ':' : '?';
    }
    nextchar = 0;
    return c;
}

int getopt_long(int argc, char *const argv[], const char *options,
                const struct option *longopts, int *longindex) {
    return getopt_internal(argc, argv, options, longopts, longindex, 0);
}

int getopt_long_only(int argc, char *const argv[], const char *options,
                     const struct option *longopts, int *longindex) {
    return getopt_internal(argc, argv, options, longopts, longindex, 1);
}
