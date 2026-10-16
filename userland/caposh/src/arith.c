#include <caposh/arith.h>
#include <caposh/error.h>
#include <caposh/expand.h>
#include <caposh/memory.h>
#include <caposh/options.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define MAX_DEPTH 64

struct value {
    long long number;
    char *name;
};

struct arith {
    const char *text;
    const char *cursor;
    int depth;
};

static struct value parse_comma(struct arith *a, int eval);

static void skip_space(struct arith *a) {
    while (isspace((unsigned char)*a->cursor)) a->cursor++;
}

static _Noreturn void arith_error(struct arith *a, const char *message) {
    sh_error("%s: %s (error token is \"%s\")", a->text, message, *a->cursor ? a->cursor : "");
}

static int accept(struct arith *a, const char *op) {
    skip_space(a);
    size_t length = strlen(op);
    if (strncmp(a->cursor, op, length)) return 0;
    if (length == 1 && strchr("=&|<>*+-", op[0]) && a->cursor[1] == op[0]) return 0;
    if (length == 1 && strchr("<>!=*/%+-&^|", op[0]) && a->cursor[1] == '=') return 0;
    if (length == 2 && (op[0] == '<' || op[0] == '>') && op[1] == op[0] && a->cursor[2] == '=')
        return 0;
    a->cursor += length;
    return 1;
}

static long long variable_value(struct arith *a, const char *name) {
    const char *value = var_get(name);
    if (!value) {
        if (option_enabled(OPTION_NOUNSET)) sh_error("%s: unbound variable", name);
        return 0;
    }
    if (!*value) return 0;
    if (++a->depth > MAX_DEPTH) arith_error(a, "expression recursion level exceeded");
    long long result = arith_evaluate(value);
    a->depth--;
    return result;
}

static long long resolve(struct arith *a, struct value v) {
    return v.name ? variable_value(a, v.name) : v.number;
}

static void store(struct arith *a, struct value target, long long number) {
    if (!target.name) arith_error(a, "attempted assignment to non-variable");
    char text[32];
    snprintf(text, sizeof(text), "%lld", number);
    if (var_set(target.name, text, 0) != 0) sh_error("%s: readonly variable", target.name);
}

static long long parse_number(struct arith *a) {
    const char *start = a->cursor;
    long long base = 10, value = 0;
    if (start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        base = 16;
        a->cursor += 2;
    } else if (start[0] == '0' && isdigit((unsigned char)start[1])) {
        base = 8;
        a->cursor++;
    } else {
        const char *hash = a->cursor;
        long long prefix = 0;
        while (isdigit((unsigned char)*hash)) prefix = prefix * 10 + (*hash++ - '0');
        if (*hash == '#') {
            if (prefix < 2 || prefix > 64) arith_error(a, "invalid arithmetic base");
            base = prefix;
            a->cursor = hash + 1;
        }
    }
    int digits = 0;
    for (;; a->cursor++) {
        int c = (unsigned char)*a->cursor, digit;
        if (isdigit(c)) digit = c - '0';
        else if (islower(c)) digit = c - 'a' + 10;
        else if (isupper(c)) digit = base <= 36 ? c - 'A' + 10 : c - 'A' + 36;
        else if (c == '@') digit = 62;
        else if (c == '_') digit = 63;
        else break;
        if (digit >= base) arith_error(a, "value too great for base");
        value = (long long)((unsigned long long)value * (unsigned long long)base + (unsigned long long)digit);
        digits++;
    }
    if (!digits && base != 8) arith_error(a, "invalid number");
    if (isalnum((unsigned char)*a->cursor)) arith_error(a, "invalid number");
    return value;
}

static struct value parse_primary(struct arith *a, int eval) {
    skip_space(a);
    struct value result = { 0, 0 };
    if (accept(a, "(")) {
        result.number = resolve(a, parse_comma(a, eval));
        if (!accept(a, ")")) arith_error(a, "missing `)'");
        return result;
    }
    if (isdigit((unsigned char)*a->cursor)) {
        result.number = parse_number(a);
        return result;
    }
    if (isalpha((unsigned char)*a->cursor) || *a->cursor == '_') {
        const char *start = a->cursor;
        while (isalnum((unsigned char)*a->cursor) || *a->cursor == '_') a->cursor++;
        result.name = ststrndup(start, (size_t)(a->cursor - start));
        return result;
    }
    if (*a->cursor == '$') {
        const char *start = a->cursor;
        a->cursor++;
        if (*a->cursor == '{') {
            while (*a->cursor && *a->cursor != '}') a->cursor++;
            if (*a->cursor) a->cursor++;
        } else {
            while (isalnum((unsigned char)*a->cursor) || *a->cursor == '_') a->cursor++;
        }
        char *text = expand_string(ststrndup(start, (size_t)(a->cursor - start)), 0);
        if (++a->depth > MAX_DEPTH) arith_error(a, "expression recursion level exceeded");
        result.number = *text ? arith_evaluate(text) : 0;
        a->depth--;
        return result;
    }
    if (!*a->cursor) arith_error(a, "syntax error: operand expected");
    arith_error(a, "syntax error: operand expected");
}

static struct value parse_postfix(struct arith *a, int eval) {
    struct value v = parse_primary(a, eval);
    skip_space(a);
    if (v.name && (!strncmp(a->cursor, "++", 2) || !strncmp(a->cursor, "--", 2))) {
        int increment = a->cursor[0] == '+';
        a->cursor += 2;
        long long old = eval ? variable_value(a, v.name) : 0;
        if (eval) store(a, v, increment ? old + 1 : old - 1);
        return (struct value) { old, 0 };
    }
    return v;
}

static struct value parse_unary(struct arith *a, int eval) {
    skip_space(a);
    if (!strncmp(a->cursor, "++", 2) || !strncmp(a->cursor, "--", 2)) {
        int increment = a->cursor[0] == '+';
        a->cursor += 2;
        struct value v = parse_unary(a, eval);
        if (!v.name) arith_error(a, "attempted assignment to non-variable");
        long long next = eval ? variable_value(a, v.name) + (increment ? 1 : -1) : 0;
        if (eval) store(a, v, next);
        return (struct value) { next, 0 };
    }
    if (accept(a, "!")) return (struct value) { !resolve(a, parse_unary(a, eval)), 0 };
    if (accept(a, "~")) return (struct value) { ~resolve(a, parse_unary(a, eval)), 0 };
    if (accept(a, "-")) return (struct value) { -resolve(a, parse_unary(a, eval)), 0 };
    if (accept(a, "+")) return (struct value) { resolve(a, parse_unary(a, eval)), 0 };
    return parse_postfix(a, eval);
}

static long long power(struct arith *a, long long base, long long exponent) {
    if (exponent < 0) arith_error(a, "exponent less than 0");
    long long result = 1;
    while (exponent--) result *= base;
    return result;
}

static struct value parse_power(struct arith *a, int eval) {
    struct value left = parse_unary(a, eval);
    skip_space(a);
    if (!strncmp(a->cursor, "**", 2) && a->cursor[2] != '=') {
        a->cursor += 2;
        long long base = resolve(a, left);
        long long exponent = resolve(a, parse_power(a, eval));
        return (struct value) { eval ? power(a, base, exponent) : 0, 0 };
    }
    return left;
}

static long long divide(struct arith *a, long long left, long long right, int modulo, int eval) {
    if (!eval) return 0;
    if (!right) arith_error(a, "division by 0");
    if (left == LLONG_MIN && right == -1) return modulo ? 0 : LLONG_MIN;
    return modulo ? left % right : left / right;
}

static struct value parse_multiplicative(struct arith *a, int eval) {
    struct value left = parse_power(a, eval);
    for (;;) {
        if (accept(a, "*")) {
            long long l = resolve(a, left), r = resolve(a, parse_power(a, eval));
            left = (struct value) { (long long)((unsigned long long)l * (unsigned long long)r), 0 };
        } else if (accept(a, "/")) {
            long long l = resolve(a, left), r = resolve(a, parse_power(a, eval));
            left = (struct value) { divide(a, l, r, 0, eval), 0 };
        } else if (accept(a, "%")) {
            long long l = resolve(a, left), r = resolve(a, parse_power(a, eval));
            left = (struct value) { divide(a, l, r, 1, eval), 0 };
        } else {
            return left;
        }
    }
}

static struct value parse_additive(struct arith *a, int eval) {
    struct value left = parse_multiplicative(a, eval);
    for (;;) {
        if (accept(a, "+")) {
            long long l = resolve(a, left), r = resolve(a, parse_multiplicative(a, eval));
            left = (struct value) { (long long)((unsigned long long)l + (unsigned long long)r), 0 };
        } else if (accept(a, "-")) {
            long long l = resolve(a, left), r = resolve(a, parse_multiplicative(a, eval));
            left = (struct value) { (long long)((unsigned long long)l - (unsigned long long)r), 0 };
        } else {
            return left;
        }
    }
}

static struct value parse_shift(struct arith *a, int eval) {
    struct value left = parse_additive(a, eval);
    for (;;) {
        if (accept(a, "<<")) {
            long long l = resolve(a, left), r = resolve(a, parse_additive(a, eval));
            left = (struct value) { (long long)((unsigned long long)l << (r & 63)), 0 };
        } else if (accept(a, ">>")) {
            long long l = resolve(a, left), r = resolve(a, parse_additive(a, eval));
            left = (struct value) { l >> (r & 63), 0 };
        } else {
            return left;
        }
    }
}

static struct value parse_relational(struct arith *a, int eval) {
    struct value left = parse_shift(a, eval);
    for (;;) {
        long long l, r;
        if (accept(a, "<=")) {
            l = resolve(a, left); r = resolve(a, parse_shift(a, eval)); left = (struct value) { l <= r, 0 };
        } else if (accept(a, ">=")) {
            l = resolve(a, left); r = resolve(a, parse_shift(a, eval)); left = (struct value) { l >= r, 0 };
        } else if (accept(a, "<")) {
            l = resolve(a, left); r = resolve(a, parse_shift(a, eval)); left = (struct value) { l < r, 0 };
        } else if (accept(a, ">")) {
            l = resolve(a, left); r = resolve(a, parse_shift(a, eval)); left = (struct value) { l > r, 0 };
        } else {
            return left;
        }
    }
}

static struct value parse_equality(struct arith *a, int eval) {
    struct value left = parse_relational(a, eval);
    for (;;) {
        if (accept(a, "==")) {
            long long l = resolve(a, left), r = resolve(a, parse_relational(a, eval));
            left = (struct value) { l == r, 0 };
        } else if (accept(a, "!=")) {
            long long l = resolve(a, left), r = resolve(a, parse_relational(a, eval));
            left = (struct value) { l != r, 0 };
        } else {
            return left;
        }
    }
}

static struct value parse_bit_and(struct arith *a, int eval) {
    struct value left = parse_equality(a, eval);
    while (accept(a, "&")) {
        long long l = resolve(a, left), r = resolve(a, parse_equality(a, eval));
        left = (struct value) { l & r, 0 };
    }
    return left;
}

static struct value parse_bit_xor(struct arith *a, int eval) {
    struct value left = parse_bit_and(a, eval);
    while (accept(a, "^")) {
        long long l = resolve(a, left), r = resolve(a, parse_bit_and(a, eval));
        left = (struct value) { l ^ r, 0 };
    }
    return left;
}

static struct value parse_bit_or(struct arith *a, int eval) {
    struct value left = parse_bit_xor(a, eval);
    while (accept(a, "|")) {
        long long l = resolve(a, left), r = resolve(a, parse_bit_xor(a, eval));
        left = (struct value) { l | r, 0 };
    }
    return left;
}

static struct value parse_logical_and(struct arith *a, int eval) {
    struct value left = parse_bit_or(a, eval);
    while (accept(a, "&&")) {
        long long l = resolve(a, left);
        long long r = resolve(a, parse_bit_or(a, eval && l));
        left = (struct value) { l && r, 0 };
    }
    return left;
}

static struct value parse_logical_or(struct arith *a, int eval) {
    struct value left = parse_logical_and(a, eval);
    while (accept(a, "||")) {
        long long l = resolve(a, left);
        long long r = resolve(a, parse_logical_and(a, eval && !l));
        left = (struct value) { l || r, 0 };
    }
    return left;
}

static struct value parse_assignment(struct arith *a, int eval);

static struct value parse_conditional(struct arith *a, int eval) {
    struct value condition = parse_logical_or(a, eval);
    if (!accept(a, "?")) return condition;
    long long test = resolve(a, condition);
    long long yes = resolve(a, parse_comma(a, eval && test));
    if (!accept(a, ":")) arith_error(a, "`:' expected for conditional expression");
    long long no = resolve(a, parse_assignment(a, eval && !test));
    return (struct value) { test ? yes : no, 0 };
}

static struct value parse_assignment(struct arith *a, int eval) {
    struct value left = parse_conditional(a, eval);
    static const char *const operators[] = {
        "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=", 0,
    };
    skip_space(a);
    for (int i = 0; operators[i]; i++) {
        size_t length = strlen(operators[i]);
        if (strncmp(a->cursor, operators[i], length)) continue;
        if (length == 1 && a->cursor[1] == '=') continue;
        a->cursor += length;
        if (!left.name) arith_error(a, "attempted assignment to non-variable");
        long long right = resolve(a, parse_assignment(a, eval));
        if (!eval) return (struct value) { 0, 0 };
        long long current = i ? variable_value(a, left.name) : 0, result = right;
        switch (operators[i][0]) {
        case '*': result = current * right; break;
        case '/': result = divide(a, current, right, 0, 1); break;
        case '%': result = divide(a, current, right, 1, 1); break;
        case '+': result = current + right; break;
        case '-': result = current - right; break;
        case '<': result = (long long)((unsigned long long)current << (right & 63)); break;
        case '>': result = current >> (right & 63); break;
        case '&': result = current & right; break;
        case '^': result = current ^ right; break;
        case '|': result = current | right; break;
        default: break;
        }
        store(a, left, result);
        return (struct value) { result, 0 };
    }
    return left;
}

static struct value parse_comma(struct arith *a, int eval) {
    struct value value = parse_assignment(a, eval);
    while (accept(a, ",")) {
        resolve(a, value);
        value = parse_assignment(a, eval);
    }
    return value;
}

long long arith_evaluate(const char *expression) {
    struct arith a = { expression, expression, 0 };
    skip_space(&a);
    if (!*a.cursor) return 0;
    long long result = resolve(&a, parse_comma(&a, 1));
    skip_space(&a);
    if (*a.cursor) arith_error(&a, "syntax error in expression");
    return result;
}
