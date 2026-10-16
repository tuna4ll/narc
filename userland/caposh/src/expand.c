#include <caposh/arith.h>
#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/expand.h>
#include <caposh/glob.h>
#include <caposh/options.h>
#include <caposh/parser.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <fnmatch.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define A_QUOTED 0x01
#define A_SPLIT  0x02
#define A_BREAK  0x04
#define A_MARK   0x08

enum case_state { CASE_WAIT_IN, CASE_PATTERN, CASE_BODY };

struct field {
    struct field *next;
    char *text;
    char *attrs;
    size_t length;
    int quoted;
};

struct expander {
    struct strbuf text;
    struct strbuf attrs;
    struct field *fields, **tail;
    int count;
    int flags;
};

static const char *expand_text(struct expander *x, const char *s, int quoted, char stop);

static int metachar(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '<' ||
           c == '>' || c == '(' || c == ')';
}

static const char *skip_single(const char *s) {
    while (*s && *s != '\'') s++;
    return *s ? s + 1 : s;
}

static const char *skip_double(const char *s);

const char *skip_backquote(const char *s) {
    while (*s && *s != '`') s += *s == '\\' && s[1] ? 2 : 1;
    return *s ? s + 1 : s;
}

static const char *skip_dollar(const char *s) {
    if (s[1] == '(') return s[2] == '(' ? skip_arithmetic(s + 3) : skip_substitution(s + 2);
    if (s[1] == '{') return skip_braced(s + 2);
    return s + 1;
}

static const char *skip_double(const char *s) {
    while (*s && *s != '"') {
        if (*s == '\\' && s[1]) s += 2;
        else if (*s == '$') s = skip_dollar(s);
        else if (*s == '`') s = skip_backquote(s + 1);
        else s++;
    }
    return *s ? s + 1 : s;
}

const char *skip_braced(const char *s) {
    int depth = 1;
    while (*s) {
        if (*s == '\\' && s[1]) s += 2;
        else if (*s == '\'') s = skip_single(s + 1);
        else if (*s == '"') s = skip_double(s + 1);
        else if (*s == '`') s = skip_backquote(s + 1);
        else if (*s == '$' && (s[1] == '(' || s[1] == '{')) s = skip_dollar(s);
        else if (*s == '{') depth++, s++;
        else if (*s == '}') {
            if (!--depth) return s + 1;
            s++;
        } else s++;
    }
    return s;
}

const char *skip_arithmetic(const char *s) {
    int depth = 0;
    while (*s) {
        if (*s == '\\' && s[1]) s += 2;
        else if (*s == '\'') s = skip_single(s + 1);
        else if (*s == '"') s = skip_double(s + 1);
        else if (*s == '`') s = skip_backquote(s + 1);
        else if (*s == '$' && (s[1] == '(' || s[1] == '{')) s = skip_dollar(s);
        else if (*s == '(') depth++, s++;
        else if (*s == ')') {
            if (!depth && s[1] == ')') return s + 2;
            depth--;
            s++;
        } else s++;
    }
    return s;
}

const char *skip_substitution(const char *s) {
    int depth = 0, cases = 0, word_start = 1;
    enum case_state states[32];
    while (*s) {
        char c = *s;
        enum case_state *state = cases ? &states[cases - 1] : 0;
        if (c == '\\' && s[1]) {
            s += 2;
            word_start = 0;
        } else if (c == '\'') {
            s = skip_single(s + 1);
            word_start = 0;
        } else if (c == '"') {
            s = skip_double(s + 1);
            word_start = 0;
        } else if (c == '`') {
            s = skip_backquote(s + 1);
            word_start = 0;
        } else if (c == '$') {
            s = skip_dollar(s);
            word_start = 0;
        } else if (c == '#' && word_start) {
            while (*s && *s != '\n') s++;
        } else if (word_start && (isalpha((unsigned char)c) || c == '_')) {
            const char *end = s;
            while (isalnum((unsigned char)*end) || *end == '_') end++;
            size_t length = (size_t)(end - s);
            if (!*end || metachar(*end)) {
                if (length == 4 && !memcmp(s, "case", 4) && cases < 32) {
                    states[cases++] = CASE_WAIT_IN;
                } else if (length == 2 && !memcmp(s, "in", 2) && state && *state == CASE_WAIT_IN) {
                    *state = CASE_PATTERN;
                } else if (length == 4 && !memcmp(s, "esac", 4) && state &&
                           *state != CASE_WAIT_IN) {
                    cases--;
                }
            }
            s = end;
            word_start = 0;
        } else if (c == '(') {
            if (!state || *state != CASE_PATTERN) depth++;
            s++;
            word_start = 1;
        } else if (c == ')') {
            if (state && *state == CASE_PATTERN) {
                *state = CASE_BODY;
            } else {
                if (!depth) return s + 1;
                depth--;
            }
            s++;
            word_start = 1;
        } else if (c == ';' && (s[1] == ';' || s[1] == '&')) {
            if (state && *state == CASE_BODY) *state = CASE_PATTERN;
            s += 2;
            if (*s == '&') s++;
            word_start = 1;
        } else {
            word_start = metachar(c);
            s++;
        }
    }
    return s;
}

static void emit(struct expander *x, const char *data, size_t length, int attr) {
    strbuf_append(&x->text, data, length);
    strbuf_reserve(&x->attrs, length);
    memset(x->attrs.data + x->attrs.length, attr, length);
    x->attrs.length += length;
    x->attrs.data[x->attrs.length] = 0;
}

static void emit_char(struct expander *x, char c, int attr) {
    emit(x, &c, 1, attr);
}

static void mark(struct expander *x) {
    emit_char(x, 0, A_MARK);
}

static void field_break(struct expander *x) {
    emit_char(x, 0, A_BREAK);
}

static const char *ifs_value(void) {
    const char *ifs = var_get("IFS");
    return ifs ? ifs : " \t\n";
}

static void emit_value(struct expander *x, const char *value, int quoted) {
    emit(x, value, strlen(value), quoted ? A_QUOTED : A_SPLIT);
}

static char *home_of(const char *user, size_t length) {
    if (!length) {
        const char *home = var_get("HOME");
        if (home) return ststrdup(home);
        struct passwd *entry = getpwuid(getuid());
        return entry ? ststrdup(entry->pw_dir) : 0;
    }
    if (length == 1 && user[0] == '+') {
        const char *pwd = var_get("PWD");
        return pwd ? ststrdup(pwd) : 0;
    }
    if (length == 1 && user[0] == '-') {
        const char *old = var_get("OLDPWD");
        return old ? ststrdup(old) : 0;
    }
    char *name = ststrndup(user, length);
    struct passwd *entry = getpwnam(name);
    return entry ? ststrdup(entry->pw_dir) : 0;
}

static const char *expand_tilde(struct expander *x, const char *s) {
    const char *end = s + 1;
    int assign = (x->flags & EXPAND_ASSIGN) != 0;
    while (*end && *end != '/' && !(assign && *end == ':')) {
        if (*end == '\\' || *end == '\'' || *end == '"' || *end == '$' || *end == '`')
            return 0;
        end++;
    }
    char *home = home_of(s + 1, (size_t)(end - s - 1));
    if (!home) return 0;
    mark(x);
    emit(x, home, strlen(home), A_QUOTED);
    return end;
}

char *expand_ansi_c(const char *text, size_t length) {
    struct strbuf out;
    strbuf_init(&out, stack);
    const char *end = text + length;
    for (const char *s = text; s < end; s++) {
        if (*s != '\\' || s + 1 >= end) {
            strbuf_putc(&out, *s);
            continue;
        }
        char c = *++s;
        switch (c) {
        case 'a': strbuf_putc(&out, '\a'); break;
        case 'b': strbuf_putc(&out, '\b'); break;
        case 'e': case 'E': strbuf_putc(&out, 0x1b); break;
        case 'f': strbuf_putc(&out, '\f'); break;
        case 'n': strbuf_putc(&out, '\n'); break;
        case 'r': strbuf_putc(&out, '\r'); break;
        case 't': strbuf_putc(&out, '\t'); break;
        case 'v': strbuf_putc(&out, '\v'); break;
        case '\\': case '\'': case '"': case '?': strbuf_putc(&out, c); break;
        case 'c':
            if (s + 1 < end) strbuf_putc(&out, (char)(*++s & 0x1f));
            break;
        case 'x': {
            int value = 0, digits = 0;
            while (digits < 2 && s + 1 < end && isxdigit((unsigned char)s[1])) {
                char h = *++s;
                value = value * 16 + (isdigit((unsigned char)h) ? h - '0' : tolower(h) - 'a' + 10);
                digits++;
            }
            if (digits) strbuf_putc(&out, (char)value);
            else strbuf_append(&out, "\\x", 2);
            break;
        }
        default:
            if (c >= '0' && c <= '7') {
                int value = c - '0', digits = 1;
                while (digits < 3 && s + 1 < end && s[1] >= '0' && s[1] <= '7') {
                    value = value * 8 + (*++s - '0');
                    digits++;
                }
                strbuf_putc(&out, (char)value);
            } else {
                strbuf_putc(&out, '\\');
                strbuf_putc(&out, c);
            }
        }
    }
    return strbuf_string(&out);
}

static const char *parameter_name_end(const char *s) {
    if (isdigit((unsigned char)*s)) {
        while (isdigit((unsigned char)*s)) s++;
        return s;
    }
    if (strchr("@*#?-$!", *s) && *s) return s + 1;
    while (isalnum((unsigned char)*s) || *s == '_') s++;
    return s;
}

static char *special_flags(void) {
    char *flags = option_flags();
    char *copy = ststrdup(flags);
    free(flags);
    return copy;
}

static const char *positional(long index) {
    if (index == 0) return shell.name;
    if (index < 0 || index > shell.argument_count) return 0;
    return shell.arguments[index - 1];
}

static const char *lookup_parameter(const char *name, size_t length, int *is_set) {
    static char number[32];
    *is_set = 1;
    if (length == 1) {
        switch (*name) {
        case '?': snprintf(number, sizeof(number), "%d", shell.status); return number;
        case '$': snprintf(number, sizeof(number), "%d", (int)shell.pid); return number;
        case '#': snprintf(number, sizeof(number), "%d", shell.argument_count); return number;
        case '!':
            if (!shell.last_background) {
                *is_set = 0;
                return 0;
            }
            snprintf(number, sizeof(number), "%d", (int)shell.last_background);
            return number;
        case '-': return special_flags();
        default: break;
        }
    }
    if (isdigit((unsigned char)*name)) {
        const char *value = positional(strtol(name, 0, 10));
        if (!value) *is_set = 0;
        return value;
    }
    char *key = ststrndup(name, length);
    const char *value = var_get(key);
    if (!value) *is_set = 0;
    return value;
}

static char *join_arguments(char separator_source) {
    (void)separator_source;
    const char *ifs = var_get("IFS");
    char separator = !ifs ? ' ' : *ifs;
    struct strbuf out;
    strbuf_init(&out, stack);
    for (int i = 0; i < shell.argument_count; i++) {
        if (i && separator) strbuf_putc(&out, separator);
        strbuf_puts(&out, shell.arguments[i]);
    }
    return strbuf_string(&out);
}

static void emit_arguments(struct expander *x, int quoted, int star) {
    if (star && quoted) {
        emit_value(x, join_arguments(0), 1);
        return;
    }
    for (int i = 0; i < shell.argument_count; i++) {
        if (i) field_break(x);
        if (quoted) mark(x);
        emit_value(x, shell.arguments[i], quoted);
    }
}

static char *expand_operand(const char *start, const char *end, int quoted, int pattern) {
    struct expander inner;
    memset(&inner, 0, sizeof(inner));
    strbuf_init(&inner.text, stack);
    strbuf_init(&inner.attrs, stack);
    inner.flags = EXPAND_TILDE;
    char *text = ststrndup(start, (size_t)(end - start));
    expand_text(&inner, text, quoted, 0);
    struct strbuf out;
    strbuf_init(&out, stack);
    for (size_t i = 0; i < inner.text.length; i++) {
        char c = inner.text.data[i];
        int attr = (unsigned char)inner.attrs.data[i];
        if (attr & (A_MARK | A_BREAK)) {
            if (attr & A_BREAK) strbuf_putc(&out, ' ');
            continue;
        }
        if (pattern && (attr & A_QUOTED) && strchr("*?[]\\", c)) strbuf_putc(&out, '\\');
        strbuf_putc(&out, c);
    }
    return strbuf_string(&out);
}

static size_t match_prefix(const char *value, const char *pattern, int longest) {
    size_t length = strlen(value);
    char *buffer = ststrdup(value);
    if (longest) {
        for (size_t n = length + 1; n-- > 0;) {
            char saved = buffer[n];
            buffer[n] = 0;
            int matched = !fnmatch(pattern, buffer, 0);
            buffer[n] = saved;
            if (matched) return n;
        }
    } else {
        for (size_t n = 0; n <= length; n++) {
            char saved = buffer[n];
            buffer[n] = 0;
            int matched = !fnmatch(pattern, buffer, 0);
            buffer[n] = saved;
            if (matched) return n;
        }
    }
    return (size_t)-1;
}

static size_t match_suffix(const char *value, const char *pattern, int longest) {
    size_t length = strlen(value);
    if (longest) {
        for (size_t start = 0; start <= length; start++)
            if (!fnmatch(pattern, value + start, 0)) return start;
    } else {
        for (size_t start = length + 1; start-- > 0;)
            if (!fnmatch(pattern, value + start, 0)) return start;
    }
    return (size_t)-1;
}

static int longest_match(char *buffer, size_t start, size_t length, const char *pattern,
                         size_t *end) {
    for (size_t candidate = length;; candidate--) {
        char saved = buffer[candidate];
        buffer[candidate] = 0;
        int matched = !fnmatch(pattern, buffer + start, 0);
        buffer[candidate] = saved;
        if (matched) {
            *end = candidate;
            return 1;
        }
        if (candidate == start) return 0;
    }
}

static char *substitute(const char *value, const char *pattern, const char *replacement,
                        int all, int anchor) {
    size_t length = strlen(value), end;
    char *buffer = ststrdup(value);
    struct strbuf out;
    strbuf_init(&out, stack);
    if (anchor == '#') {
        if (!longest_match(buffer, 0, length, pattern, &end)) return buffer;
        strbuf_puts(&out, replacement);
        strbuf_puts(&out, value + end);
        return strbuf_string(&out);
    }
    if (anchor == '%') {
        for (size_t start = 0; start <= length; start++) {
            if (fnmatch(pattern, value + start, 0)) continue;
            strbuf_append(&out, value, start);
            strbuf_puts(&out, replacement);
            return strbuf_string(&out);
        }
        return buffer;
    }
    size_t start = 0;
    while (start <= length) {
        if (*pattern && longest_match(buffer, start, length, pattern, &end) && end > start) {
            strbuf_puts(&out, replacement);
            start = end;
            if (!all) {
                strbuf_puts(&out, value + start);
                return strbuf_string(&out);
            }
            continue;
        }
        if (start < length) strbuf_putc(&out, value[start]);
        start++;
    }
    return strbuf_string(&out);
}

static char *case_convert(const char *value, const char *pattern, int upper, int all) {
    char *copy = ststrdup(value);
    for (char *c = copy; *c; c++) {
        char single[2] = { *c, 0 };
        if (!*pattern || !fnmatch(pattern, single, 0))
            *c = (char)(upper ? toupper((unsigned char)*c) : tolower((unsigned char)*c));
        if (!all) break;
    }
    return copy;
}

static char *substring(const char *value, const char *spec) {
    const char *colon = 0;
    int depth = 0;
    for (const char *s = spec; *s; s++) {
        if (*s == '(' ) depth++;
        else if (*s == ')') depth--;
        else if (*s == '?' ) depth++;
        else if (*s == ':' && depth > 0) depth--;
        else if (*s == ':' && !depth) {
            colon = s;
            break;
        }
    }
    long long length = (long long)strlen(value);
    long long offset = arith_evaluate(colon ? ststrndup(spec, (size_t)(colon - spec)) : spec);
    if (offset < 0) offset += length;
    if (offset < 0 || offset > length) return ststrdup("");
    long long count = length - offset;
    if (colon) {
        count = arith_evaluate(colon + 1);
        if (count < 0) count = length - offset + count;
        if (count < 0) sh_error("%s: substring expression < 0", colon + 1);
        if (offset + count > length) count = length - offset;
    }
    return ststrndup(value + offset, (size_t)count);
}

static void unset_error(const char *name, size_t length) {
    sh_error("%.*s: unbound variable", (int)length, name);
}

static const char *expand_braced(struct expander *x, const char *s, int quoted) {
    const char *end = skip_braced(s) - 1;
    const char *cursor = s;
    int length_of = 0, indirect = 0;
    if (*cursor == '#' && cursor + 1 < end && !strchr(":-=?+%#}", cursor[1])) {
        length_of = 1;
        cursor++;
    } else if (*cursor == '#' && cursor + 1 == end) {
        length_of = 0;
    } else if (*cursor == '!' && cursor + 1 < end && cursor[1] != '}') {
        indirect = 1;
        cursor++;
    }
    const char *name = cursor;
    const char *name_end = parameter_name_end(cursor);
    if (name_end == name) sh_error("${%.*s}: bad substitution", (int)(end - s), s);
    size_t name_length = (size_t)(name_end - name);
    int is_set;
    int all_arguments = name_length == 1 && (*name == '@' || *name == '*');
    const char *value = all_arguments ? 0 : lookup_parameter(name, name_length, &is_set);
    if (all_arguments) {
        is_set = shell.argument_count > 0;
        value = join_arguments(0);
    }
    if (indirect) {
        if (!value || (!is_name(value) && !isdigit((unsigned char)*value)))
            sh_error("%s: invalid indirect expansion", value ? value : "");
        value = lookup_parameter(value, strlen(value), &is_set);
    }
    cursor = name_end;
    if (length_of) {
        if (cursor != end) sh_error("${%.*s}: bad substitution", (int)(end - s), s);
        if (!is_set && option_enabled(OPTION_NOUNSET) && !all_arguments)
            unset_error(name, name_length);
        char number[32];
        snprintf(number, sizeof(number), "%zu",
                 all_arguments ? (size_t)shell.argument_count : value ? strlen(value) : 0);
        emit_value(x, number, quoted);
        return end + 1;
    }
    if (cursor == end) {
        if (!is_set && option_enabled(OPTION_NOUNSET) && !all_arguments)
            unset_error(name, name_length);
        if (all_arguments) emit_arguments(x, quoted, *name == '*');
        else if (value) emit_value(x, value, quoted);
        return end + 1;
    }
    int colon = *cursor == ':';
    char op = cursor[colon];
    const char *operand = cursor + colon + 1;
    int empty = !is_set || (colon && (!value || !*value));
    if (colon && !strchr("-=?+", op)) {
        if (!is_set && option_enabled(OPTION_NOUNSET)) unset_error(name, name_length);
        char *spec = expand_operand(cursor + 1, end, 1, 0);
        emit_value(x, substring(value ? value : "", spec), quoted);
        return end + 1;
    }
    switch (op) {
    case '-':
        if (empty) {
            expand_text(x, ststrndup(operand, (size_t)(end - operand)), quoted, 0);
        } else if (all_arguments) {
            emit_arguments(x, quoted, *name == '*');
        } else {
            emit_value(x, value, quoted);
        }
        return end + 1;
    case '+':
        if (!empty) expand_text(x, ststrndup(operand, (size_t)(end - operand)), quoted, 0);
        return end + 1;
    case '=': {
        if (!empty) {
            emit_value(x, value, quoted);
            return end + 1;
        }
        if (!is_name_prefix(name, name_length))
            sh_error("$%.*s: cannot assign in this way", (int)name_length, name);
        char *assigned = expand_operand(operand, end, quoted, 0);
        if (var_set(ststrndup(name, name_length), assigned, 0) != 0) raise_exception(EXCEPTION_ERROR);
        emit_value(x, assigned, quoted);
        return end + 1;
    }
    case '?':
        if (empty) {
            char *message = expand_operand(operand, end, 1, 0);
            sh_error("%.*s: %s", (int)name_length, name,
                     *message ? message : "parameter null or not set");
        }
        emit_value(x, value, quoted);
        return end + 1;
    default:
        break;
    }
    if (!is_set && option_enabled(OPTION_NOUNSET)) unset_error(name, name_length);
    const char *subject = value ? value : "";
    char *result;
    if (op == '#' || op == '%') {
        int longest = cursor[1] == op;
        char *pattern = expand_operand(cursor + 1 + longest, end, quoted, 1);
        size_t cut = op == '#' ? match_prefix(subject, pattern, longest) :
                                 match_suffix(subject, pattern, longest);
        if (cut == (size_t)-1) result = ststrdup(subject);
        else result = op == '#' ? ststrdup(subject + cut) : ststrndup(subject, cut);
    } else if (op == '/') {
        const char *p = cursor + 1;
        int all = 0, anchor = 0;
        if (*p == '/') all = 1, p++;
        else if (*p == '#' || *p == '%') anchor = *p++;
        const char *slash = p;
        while (slash < end && *slash != '/') slash += *slash == '\\' && slash + 1 < end ? 2 : 1;
        char *pattern = expand_operand(p, slash, quoted, 1);
        char *replacement = slash < end ? expand_operand(slash + 1, end, quoted, 0) : "";
        result = substitute(subject, pattern, replacement, all, anchor);
    } else if (op == '^' || op == ',') {
        int all = cursor[1] == op;
        char *pattern = expand_operand(cursor + 1 + all, end, quoted, 1);
        result = case_convert(subject, pattern, op == '^', all);
    } else {
        sh_error("${%.*s}: bad substitution", (int)(end - s), s);
    }
    emit_value(x, result, quoted);
    return end + 1;
}

static char *unescape_backquote(const char *start, const char *end, int quoted) {
    struct strbuf out;
    strbuf_init(&out, stack);
    for (const char *s = start; s < end; s++) {
        if (*s == '\\' && s + 1 < end &&
            (s[1] == '$' || s[1] == '`' || s[1] == '\\' || (quoted && s[1] == '"')))
            s++;
        strbuf_putc(&out, *s);
    }
    return strbuf_string(&out);
}

static void emit_substitution(struct expander *x, const char *command, int quoted) {
    char *output = command_substitute(command);
    emit_value(x, output, quoted);
}

static const char *expand_dollar(struct expander *x, const char *s, int quoted) {
    const char *after = s + 1;
    if (*after == '(' && after[1] == '(') {
        const char *end = skip_arithmetic(after + 2);
        const char *inner_end = end - 2;
        char *expression = expand_operand(after + 2, inner_end, 1, 0);
        long long value = arith_evaluate(expression);
        char number[32];
        snprintf(number, sizeof(number), "%lld", value);
        emit_value(x, number, quoted);
        return end;
    }
    if (*after == '(') {
        const char *end = skip_substitution(after + 1);
        emit_substitution(x, ststrndup(after + 1, (size_t)(end - after - 2)), quoted);
        return end;
    }
    if (*after == '{') return expand_braced(x, after + 1, quoted);
    if (*after == '\'' && !quoted) {
        const char *end = after + 1;
        while (*end && *end != '\'') end += *end == '\\' && end[1] ? 2 : 1;
        mark(x);
        char *text = expand_ansi_c(after + 1, (size_t)(end - after - 1));
        emit(x, text, strlen(text), A_QUOTED);
        return *end ? end + 1 : end;
    }
    if (*after == '"' && !quoted) {
        return expand_text(x, after + 1, 1, '"');
    }
    const char *name_end = parameter_name_end(after);
    if (name_end == after) {
        emit_char(x, '$', quoted ? A_QUOTED : 0);
        return after;
    }
    if (isdigit((unsigned char)*after)) name_end = after + 1;
    size_t length = (size_t)(name_end - after);
    if (length == 1 && (*after == '@' || *after == '*')) {
        emit_arguments(x, quoted, *after == '*');
        return name_end;
    }
    int is_set;
    const char *value = lookup_parameter(after, length, &is_set);
    if (!is_set && option_enabled(OPTION_NOUNSET)) unset_error(after, length);
    if (value) emit_value(x, value, quoted);
    return name_end;
}

static const char *expand_text(struct expander *x, const char *s, int quoted, char stop) {
    size_t mark_position = x->text.length;
    int produced = 0, only_arguments = 0;
    if (stop == '"') mark(x);
    int at_start = 1;
    while (*s) {
        char c = *s;
        if (stop && c == stop) {
            s++;
            break;
        }
        if (c == '\\') {
            char next = s[1];
            if (!next) {
                emit_char(x, '\\', quoted ? A_QUOTED : 0);
                s++;
                continue;
            }
            if (quoted && !strchr("$`\"\\\n", next)) {
                emit_char(x, '\\', A_QUOTED);
                s++;
                produced = 1;
                continue;
            }
            if (next != '\n') emit_char(x, next, A_QUOTED);
            s += 2;
            produced = 1;
        } else if (c == '\'' && !quoted) {
            const char *end = skip_single(s + 1);
            size_t length = (size_t)(end - s - 1) - (*(end - 1) == '\'' ? 1 : 0);
            mark(x);
            emit(x, s + 1, length, A_QUOTED);
            s = end;
            produced = 1;
        } else if (c == '"' && !quoted) {
            s = expand_text(x, s + 1, 1, '"');
            produced = 1;
        } else if (c == '$') {
            if (quoted && s[1] == '@' && (!s[2] || s[2] == '"')) only_arguments = 1;
            else produced = 1;
            s = expand_dollar(x, s, quoted);
        } else if (c == '`') {
            const char *end = skip_backquote(s + 1);
            const char *inner_end = *(end - 1) == '`' ? end - 1 : end;
            emit_substitution(x, unescape_backquote(s + 1, inner_end, quoted), quoted);
            s = end;
            produced = 1;
        } else if (c == '~' && !quoted &&
                   ((at_start && (x->flags & EXPAND_TILDE)) ||
                    ((x->flags & EXPAND_ASSIGN) && x->text.length &&
                     x->text.data[x->text.length - 1] == ':'))) {
            const char *after = expand_tilde(x, s);
            if (after) s = after;
            else emit_char(x, *s++, 0);
            produced = 1;
        } else {
            emit_char(x, c, quoted ? A_QUOTED : 0);
            s++;
            produced = 1;
        }
        at_start = 0;
    }
    if (stop == '"' && !produced && only_arguments && shell.argument_count == 0 &&
        x->text.length == mark_position + 1) {
        strbuf_truncate(&x->text, mark_position);
        strbuf_truncate(&x->attrs, mark_position);
    }
    return s;
}

static void push_field(struct expander *x, struct strbuf *text, struct strbuf *attrs, int quoted) {
    struct field *field = stalloc(sizeof(*field));
    field->next = 0;
    field->text = strbuf_string(text);
    field->attrs = strbuf_string(attrs);
    field->length = text->length;
    field->quoted = quoted;
    *x->tail = field;
    x->tail = &field->next;
    x->count++;
    strbuf_init(text, stack);
    strbuf_init(attrs, stack);
}

static void split(struct expander *x) {
    const char *ifs = ifs_value();
    int split_enabled = !(x->flags & EXPAND_NOSPLIT);
    struct strbuf text, attrs;
    strbuf_init(&text, stack);
    strbuf_init(&attrs, stack);
    int quoted = 0, started = 0;
    size_t length = x->text.length;
    for (size_t i = 0; i < length; i++) {
        char c = x->text.data[i];
        int attr = (unsigned char)x->attrs.data[i];
        if (attr & A_MARK) {
            quoted = started = 1;
            continue;
        }
        if (attr & A_BREAK) {
            push_field(x, &text, &attrs, quoted);
            quoted = 0;
            started = 0;
            continue;
        }
        if (split_enabled && (attr & A_SPLIT) && c && strchr(ifs, c)) {
            int whitespace = c == ' ' || c == '\t' || c == '\n';
            if (whitespace) {
                while (i + 1 < length && (x->attrs.data[i + 1] & A_SPLIT) &&
                       strchr(ifs, x->text.data[i + 1]) && x->text.data[i + 1] &&
                       strchr(" \t\n", x->text.data[i + 1]))
                    i++;
                if (i + 1 < length && (x->attrs.data[i + 1] & A_SPLIT) && x->text.data[i + 1] &&
                    strchr(ifs, x->text.data[i + 1])) {
                    i++;
                    whitespace = 0;
                }
            }
            if (started || text.length || !whitespace) push_field(x, &text, &attrs, quoted);
            if (!whitespace) {
                while (i + 1 < length && (x->attrs.data[i + 1] & A_SPLIT) && x->text.data[i + 1] &&
                       strchr(" \t\n", x->text.data[i + 1]) && strchr(ifs, x->text.data[i + 1]))
                    i++;
            }
            quoted = started = 0;
            continue;
        }
        strbuf_putc(&text, c);
        strbuf_putc(&attrs, (char)attr);
        started = 1;
    }
    if (text.length || quoted) push_field(x, &text, &attrs, quoted);
}

static int has_glob(const struct field *field) {
    for (size_t i = 0; i < field->length; i++)
        if (!(field->attrs[i] & A_QUOTED) && strchr("*?[", field->text[i])) return 1;
    return 0;
}

static char *glob_pattern(const struct field *field) {
    struct strbuf out;
    strbuf_init(&out, stack);
    for (size_t i = 0; i < field->length; i++) {
        char c = field->text[i];
        if ((field->attrs[i] & A_QUOTED) && strchr("*?[]\\", c)) strbuf_putc(&out, '\\');
        else if (!(field->attrs[i] & A_QUOTED) && c == '\\') strbuf_putc(&out, '\\');
        strbuf_putc(&out, c);
    }
    return strbuf_string(&out);
}

static void run(struct expander *x, const char *text, int flags) {
    memset(x, 0, sizeof(*x));
    strbuf_init(&x->text, stack);
    strbuf_init(&x->attrs, stack);
    x->tail = &x->fields;
    x->flags = flags;
    expand_text(x, text, 0, 0);
}

char **expand_words(struct word *words, int *count) {
    struct field *all = 0, **tail = &all;
    int total = 0;
    for (struct word *word = words; word; word = word->next) {
        shell.line = word->line;
        struct expander x;
        run(&x, word->text, EXPAND_TILDE);
        split(&x);
        for (struct field *field = x.fields; field; field = field->next) {
            if (!option_enabled(OPTION_NOGLOB) && has_glob(field)) {
                char **matches;
                int found = glob_expand(glob_pattern(field), &matches);
                if (found > 0) {
                    for (int i = 0; i < found; i++) {
                        struct field *match = stalloc(sizeof(*match));
                        match->text = matches[i];
                        match->attrs = 0;
                        match->length = strlen(matches[i]);
                        match->quoted = 1;
                        match->next = 0;
                        *tail = match;
                        tail = &match->next;
                        total++;
                    }
                    continue;
                }
            }
            struct field *copy = stalloc(sizeof(*copy));
            *copy = *field;
            copy->next = 0;
            *tail = copy;
            tail = &copy->next;
            total++;
        }
    }
    char **argv = stalloc(((size_t)total + 1) * sizeof(char *));
    int index = 0;
    for (struct field *field = all; field; field = field->next) argv[index++] = field->text;
    argv[index] = 0;
    if (count) *count = total;
    return argv;
}

static char *flatten(struct expander *x) {
    struct strbuf out;
    strbuf_init(&out, stack);
    for (size_t i = 0; i < x->text.length; i++) {
        int attr = (unsigned char)x->attrs.data[i];
        if (attr & A_MARK) continue;
        if (attr & A_BREAK) {
            strbuf_putc(&out, ' ');
            continue;
        }
        strbuf_putc(&out, x->text.data[i]);
    }
    return strbuf_string(&out);
}

char *expand_string(const char *text, int flags) {
    struct expander x;
    run(&x, text, flags);
    return flatten(&x);
}

char *expand_assignment(const char *text) {
    struct expander x;
    run(&x, text, EXPAND_TILDE | EXPAND_ASSIGN);
    return flatten(&x);
}

char *expand_pattern(const char *text) {
    struct expander x;
    run(&x, text, EXPAND_TILDE);
    struct strbuf out;
    strbuf_init(&out, stack);
    for (size_t i = 0; i < x.text.length; i++) {
        char c = x.text.data[i];
        int attr = (unsigned char)x.attrs.data[i];
        if (attr & (A_MARK | A_BREAK)) continue;
        if ((attr & A_QUOTED) && strchr("*?[]\\", c)) strbuf_putc(&out, '\\');
        strbuf_putc(&out, c);
    }
    return strbuf_string(&out);
}

char *expand_heredoc(const char *body) {
    struct expander x;
    memset(&x, 0, sizeof(x));
    strbuf_init(&x.text, stack);
    strbuf_init(&x.attrs, stack);
    x.tail = &x.fields;
    struct strbuf out;
    strbuf_init(&out, stack);
    for (const char *s = body; *s;) {
        if (*s == '\\' && (s[1] == '$' || s[1] == '`' || s[1] == '\\')) {
            emit_char(&x, s[1], A_QUOTED);
            s += 2;
        } else if (*s == '$') {
            s = expand_dollar(&x, s, 1);
        } else if (*s == '`') {
            const char *end = skip_backquote(s + 1);
            const char *inner_end = *(end - 1) == '`' ? end - 1 : end;
            emit_substitution(&x, unescape_backquote(s + 1, inner_end, 0), 1);
            s = end;
        } else {
            emit_char(&x, *s++, A_QUOTED);
        }
    }
    return flatten(&x);
}

char *expand_single(struct word *word, const char *context) {
    int count;
    char **fields = expand_words(word, &count);
    if (count != 1) sh_error("%s: ambiguous redirect", context ? context : word->text);
    return fields[0];
}

char *expand_prompt_parameters(const char *text) {
    struct expander x;
    run(&x, text, 0);
    return flatten(&x);
}
