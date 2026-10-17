#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct test {
    char **argv;
    int argc, index;
    int error;
    const char *name;
};

static int unary_operator(const char *op) {
    return op[0] == '-' && op[1] && !op[2] && strchr("abcdefghknoprstuvwxzGLNOS", op[1]);
}

static int binary_operator(const char *op) {
    static const char *const ops[] = {
        "=", "==", "!=", "<", ">", "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef", 0,
    };
    for (int i = 0; ops[i]; i++) if (!strcmp(op, ops[i])) return 1;
    return 0;
}

static int parse_integer(const char *text, long long *value) {
    while (isspace((unsigned char)*text)) text++;
    if (!*text) return -1;
    char *end;
    errno = 0;
    *value = strtoll(text, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    return *end || errno ? -1 : 0;
}

int test_unary(const char *op, const char *operand) {
    struct stat info;
    switch (op[1]) {
    case 'z': return !*operand;
    case 'n': return *operand != 0;
    case 't': {
        long long fd;
        return parse_integer(operand, &fd) == 0 && isatty((int)fd);
    }
    case 'v': return var_get(operand) != 0;
    case 'o': return 0;
    case 'h': case 'L': return lstat(operand, &info) == 0 && S_ISLNK(info.st_mode);
    default: break;
    }
    if (stat(operand, &info) != 0) return 0;
    switch (op[1]) {
    case 'a': case 'e': return 1;
    case 'b': return S_ISBLK(info.st_mode);
    case 'c': return S_ISCHR(info.st_mode);
    case 'd': return S_ISDIR(info.st_mode);
    case 'f': return S_ISREG(info.st_mode);
    case 'g': return (info.st_mode & S_ISGID) != 0;
    case 'k': return (info.st_mode & S_ISVTX) != 0;
    case 'p': return S_ISFIFO(info.st_mode);
    case 'r': return access(operand, R_OK) == 0;
    case 'w': return access(operand, W_OK) == 0;
    case 'x': return access(operand, X_OK) == 0;
    case 's': return info.st_size > 0;
    case 'S': return S_ISSOCK(info.st_mode);
    case 'u': return (info.st_mode & S_ISUID) != 0;
    case 'G': return info.st_gid == getegid();
    case 'O': return info.st_uid == geteuid();
    case 'N': return 1;
    default: return 0;
    }
}

int test_binary(const char *left, const char *op, const char *right, int *error) {
    if (!strcmp(op, "=") || !strcmp(op, "==")) return !strcmp(left, right);
    if (!strcmp(op, "!=")) return strcmp(left, right) != 0;
    if (!strcmp(op, "<")) return strcmp(left, right) < 0;
    if (!strcmp(op, ">")) return strcmp(left, right) > 0;
    if (!strcmp(op, "-ef") || !strcmp(op, "-nt") || !strcmp(op, "-ot")) {
        struct stat a, b;
        int has_a = stat(left, &a) == 0, has_b = stat(right, &b) == 0;
        if (op[1] == 'e') return has_a && has_b && a.st_ino == b.st_ino && a.st_dev == b.st_dev;
        if (op[1] == 'n') return has_a && (!has_b || a.st_ino > b.st_ino);
        return has_b && (!has_a || a.st_ino < b.st_ino);
    }
    long long a, b;
    if (parse_integer(left, &a) != 0 || parse_integer(right, &b) != 0) {
        *error = 1;
        builtin_error("test", "%s: integer expression expected",
                      parse_integer(left, &a) ? left : right);
        return 0;
    }
    if (!strcmp(op, "-eq")) return a == b;
    if (!strcmp(op, "-ne")) return a != b;
    if (!strcmp(op, "-lt")) return a < b;
    if (!strcmp(op, "-le")) return a <= b;
    if (!strcmp(op, "-gt")) return a > b;
    if (!strcmp(op, "-ge")) return a >= b;
    *error = 1;
    return 0;
}

static const char *peek(struct test *t, int offset) {
    int at = t->index + offset;
    return at < t->argc ? t->argv[at] : 0;
}

static void syntax(struct test *t, const char *message, const char *token) {
    if (!t->error) builtin_error(t->name, token ? "%s: %s" : "%s", token ? token : message, message);
    t->error = 1;
}

static int parse_or(struct test *t);

static int parse_primary(struct test *t) {
    const char *token = peek(t, 0);
    if (!token) {
        syntax(t, "argument expected", 0);
        return 0;
    }
    if (!strcmp(token, "!")) {
        t->index++;
        return !parse_primary(t);
    }
    if (!strcmp(token, "(") && peek(t, 1)) {
        const char *after = peek(t, 2);
        if (!(after && binary_operator(peek(t, 1)) && peek(t, 3))) {
            t->index++;
            int value = parse_or(t);
            if (!peek(t, 0) || strcmp(peek(t, 0), ")")) syntax(t, "`)' expected", 0);
            else t->index++;
            return value;
        }
    }
    const char *next = peek(t, 1);
    if (next && binary_operator(next) && peek(t, 2)) {
        t->index += 3;
        return test_binary(token, next, t->argv[t->index - 1], &t->error);
    }
    if (unary_operator(token) && next) {
        t->index += 2;
        return test_unary(token, next);
    }
    t->index++;
    return *token != 0;
}

static int parse_and(struct test *t) {
    int value = parse_primary(t);
    while (peek(t, 0) && !strcmp(peek(t, 0), "-a")) {
        t->index++;
        int right = parse_primary(t);
        value = value && right;
    }
    return value;
}

static int parse_or(struct test *t) {
    int value = parse_and(t);
    while (peek(t, 0) && !strcmp(peek(t, 0), "-o")) {
        t->index++;
        int right = parse_and(t);
        value = value || right;
    }
    return value;
}

static int evaluate_count(struct test *t, int count) {
    char **a = t->argv + t->index;
    switch (count) {
    case 0: return 0;
    case 1: t->index++; return *a[0] != 0;
    case 2:
        if (!strcmp(a[0], "!")) { t->index += 2; return *a[1] == 0; }
        if (unary_operator(a[0])) { t->index += 2; return test_unary(a[0], a[1]); }
        break;
    case 3:
        if (binary_operator(a[1])) { t->index += 3; return test_binary(a[0], a[1], a[2], &t->error); }
        if (!strcmp(a[0], "!")) { t->index++; return !evaluate_count(t, 2); }
        if (!strcmp(a[0], "(") && !strcmp(a[2], ")")) { t->index += 3; return *a[1] != 0; }
        break;
    case 4:
        if (!strcmp(a[0], "!")) { t->index++; return !evaluate_count(t, 3); }
        if (!strcmp(a[0], "(") && !strcmp(a[3], ")")) {
            t->index++;
            int value = evaluate_count(t, 2);
            t->index++;
            return value;
        }
        break;
    default:
        break;
    }
    return parse_or(t);
}

int test_evaluate(int argc, char **argv, int *status) {
    struct test t = { argv, argc, 0, 0, "test" };
    int value = evaluate_count(&t, argc);
    if (!t.error && t.index < t.argc) syntax(&t, "too many arguments", 0);
    *status = t.error ? 2 : value ? 0 : 1;
    return *status;
}

int builtin_test(int argc, char **argv) {
    if (!strcmp(argv[0], "[")) {
        if (strcmp(argv[argc - 1], "]")) {
            builtin_error("[", "missing `]'");
            return 2;
        }
        argc--;
    }
    int status;
    test_evaluate(argc - 1, argv + 1, &status);
    return status;
}
