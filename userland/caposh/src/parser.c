#include <caposh/alias.h>
#include <caposh/error.h>
#include <caposh/input.h>
#include <caposh/options.h>
#include <caposh/parser.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum token_type {
    TOKEN_EOF,
    TOKEN_NEWLINE,
    TOKEN_WORD,
    TOKEN_IO_NUMBER,
    TOKEN_ARITH,
    TOKEN_AND_IF,
    TOKEN_OR_IF,
    TOKEN_DSEMI,
    TOKEN_SEMI_AND,
    TOKEN_SEMI_SEMI_AND,
    TOKEN_DLESS,
    TOKEN_DLESSDASH,
    TOKEN_TLESS,
    TOKEN_DGREAT,
    TOKEN_LESSAND,
    TOKEN_GREATAND,
    TOKEN_LESSGREAT,
    TOKEN_CLOBBER,
    TOKEN_AND_GREAT,
    TOKEN_AND_DGREAT,
    TOKEN_PIPE,
    TOKEN_PIPE_AND,
    TOKEN_AMP,
    TOKEN_SEMI,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LESS,
    TOKEN_GREAT,
};

struct token {
    enum token_type type;
    char *text;
    int quoted;
    int line;
};

struct parser {
    struct arena *arena;
    struct token token;
    int peeked;
    struct redirect *heredocs[64];
    int heredoc_count;
    int nested;
    int aliases;
    int alias_next;
    int command_position;
};

static const char *const token_names[] = {
    [TOKEN_EOF] = "end of file", [TOKEN_NEWLINE] = "newline", [TOKEN_AND_IF] = "&&",
    [TOKEN_OR_IF] = "||", [TOKEN_DSEMI] = ";;", [TOKEN_SEMI_AND] = ";&",
    [TOKEN_SEMI_SEMI_AND] = ";;&", [TOKEN_DLESS] = "<<", [TOKEN_DLESSDASH] = "<<-",
    [TOKEN_TLESS] = "<<<", [TOKEN_DGREAT] = ">>", [TOKEN_LESSAND] = "<&",
    [TOKEN_GREATAND] = ">&", [TOKEN_LESSGREAT] = "<>", [TOKEN_CLOBBER] = ">|",
    [TOKEN_AND_GREAT] = "&>", [TOKEN_AND_DGREAT] = "&>>", [TOKEN_PIPE] = "|",
    [TOKEN_PIPE_AND] = "|&", [TOKEN_AMP] = "&", [TOKEN_SEMI] = ";", [TOKEN_LPAREN] = "(",
    [TOKEN_RPAREN] = ")", [TOKEN_LESS] = "<", [TOKEN_GREAT] = ">",
};

static struct node *parse_compound_list(struct parser *p);
static struct node *parse_and_or(struct parser *p);
static struct node *parse_command(struct parser *p);
static void read_heredocs(struct parser *p);

int is_name_prefix(const char *text, size_t length) {
    if (!length || (!isalpha((unsigned char)text[0]) && text[0] != '_')) return 0;
    for (size_t i = 1; i < length; i++)
        if (!isalnum((unsigned char)text[i]) && text[i] != '_') return 0;
    return 1;
}

int is_name(const char *text) {
    return is_name_prefix(text, strlen(text));
}

size_t assignment_name_length(const char *text) {
    const char *equals = strchr(text, '=');
    if (!equals || equals == text) return 0;
    size_t length = (size_t)(equals - text);
    if (text[length - 1] == '+') length--;
    return is_name_prefix(text, length) ? length : 0;
}

static _Noreturn void unexpected(struct token *token) {
    if (token->type == TOKEN_EOF) sh_syntax_error("unexpected end of file");
    const char *text = token->type == TOKEN_WORD || token->type == TOKEN_IO_NUMBER ||
                       token->type == TOKEN_ARITH ? token->text : token_names[token->type];
    sh_syntax_error("unexpected token `%s'", text);
}

static _Noreturn void unterminated(const char *what) {
    sh_syntax_error("unexpected end of file while looking for matching `%s'", what);
}

static void scan_dollar(struct parser *p, struct strbuf *buffer);
static void scan_backquote(struct strbuf *buffer);

static void scan_single_quoted(struct strbuf *buffer, int ansi) {
    for (;;) {
        int c = input_getc();
        if (c == INPUT_EOF) unterminated("'");
        strbuf_putc(buffer, (char)c);
        if (c == '\'') return;
        if (ansi && c == '\\') {
            c = input_getc();
            if (c == INPUT_EOF) unterminated("'");
            strbuf_putc(buffer, (char)c);
        }
    }
}

static void scan_double_quoted(struct parser *p, struct strbuf *buffer) {
    for (;;) {
        int c = input_getc();
        if (c == INPUT_EOF) unterminated("\"");
        if (c == '\\') {
            int next = input_getc();
            if (next == '\n') continue;
            strbuf_putc(buffer, '\\');
            if (next == INPUT_EOF) unterminated("\"");
            strbuf_putc(buffer, (char)next);
            continue;
        }
        strbuf_putc(buffer, (char)c);
        if (c == '"') return;
        if (c == '$') scan_dollar(p, buffer);
        else if (c == '`') scan_backquote(buffer);
    }
}

static void scan_backquote(struct strbuf *buffer) {
    for (;;) {
        int c = input_getc();
        if (c == INPUT_EOF) unterminated("`");
        strbuf_putc(buffer, (char)c);
        if (c == '`') return;
        if (c == '\\') {
            c = input_getc();
            if (c == INPUT_EOF) unterminated("`");
            strbuf_putc(buffer, (char)c);
        }
    }
}

static void scan_arithmetic(struct parser *p, struct strbuf *buffer) {
    int depth = 0;
    for (;;) {
        int c = input_getc();
        if (c == INPUT_EOF) unterminated("))");
        if (c == ')' && !depth) {
            int next = input_getc();
            if (next == ')') {
                strbuf_append(buffer, "))", 2);
                return;
            }
            input_ungetc(next);
            strbuf_putc(buffer, ')');
            continue;
        }
        strbuf_putc(buffer, (char)c);
        if (c == '(') depth++;
        else if (c == ')') depth--;
        else if (c == '\'') scan_single_quoted(buffer, 0);
        else if (c == '"') scan_double_quoted(p, buffer);
        else if (c == '`') scan_backquote(buffer);
        else if (c == '$') scan_dollar(p, buffer);
        else if (c == '\\') {
            c = input_getc();
            if (c == INPUT_EOF) unterminated("))");
            strbuf_putc(buffer, (char)c);
        }
    }
}

static void parser_init(struct parser *p, struct arena *arena) {
    memset(p, 0, sizeof(*p));
    p->arena = arena;
    p->aliases = 1;
}

static void next(struct parser *p);
static void command_start(struct parser *p);

static void scan_command_substitution(struct parser *p, struct strbuf *buffer) {
    struct strbuf capture;
    strbuf_init(&capture, p->arena);
    struct parser inner;
    parser_init(&inner, p->arena);
    inner.nested = 1;
    inner.aliases = 0;
    input_capture_begin(&capture);
    command_start(&inner);
    struct node *body = parse_compound_list(&inner);
    (void)body;
    if (inner.token.type != TOKEN_RPAREN) {
        input_capture_end();
        if (inner.token.type == TOKEN_EOF) unterminated(")");
        unexpected(&inner.token);
    }
    input_capture_end();
    if (capture.length && capture.data[capture.length - 1] == ')')
        strbuf_truncate(&capture, capture.length - 1);
    strbuf_append(buffer, capture.data ? capture.data : "", capture.length);
    strbuf_putc(buffer, ')');
}

static void scan_braced(struct parser *p, struct strbuf *buffer) {
    int depth = 1;
    for (;;) {
        int c = input_getc();
        if (c == INPUT_EOF) unterminated("}");
        strbuf_putc(buffer, (char)c);
        if (c == '}' && !--depth) return;
        if (c == '{') depth++;
        else if (c == '\\') {
            c = input_getc();
            if (c == INPUT_EOF) unterminated("}");
            strbuf_putc(buffer, (char)c);
        } else if (c == '\'') {
            scan_single_quoted(buffer, 0);
        } else if (c == '"') {
            scan_double_quoted(p, buffer);
        } else if (c == '`') {
            scan_backquote(buffer);
        } else if (c == '$') {
            int after = input_peek();
            if (after == '{') {
                strbuf_putc(buffer, (char)input_getc());
                scan_braced(p, buffer);
            } else {
                scan_dollar(p, buffer);
            }
        }
    }
}

static void scan_dollar(struct parser *p, struct strbuf *buffer) {
    int c = input_getc();
    if (c == '(') {
        strbuf_putc(buffer, '(');
        int after = input_getc();
        if (after == '(') {
            strbuf_putc(buffer, '(');
            scan_arithmetic(p, buffer);
            return;
        }
        input_ungetc(after);
        scan_command_substitution(p, buffer);
    } else if (c == '{') {
        strbuf_putc(buffer, '{');
        scan_braced(p, buffer);
    } else if (c == '\'') {
        strbuf_putc(buffer, '\'');
        scan_single_quoted(buffer, 1);
    } else if (c == '"') {
        strbuf_putc(buffer, '"');
        scan_double_quoted(p, buffer);
    } else {
        input_ungetc(c);
    }
}

static int metacharacter(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' ||
           c == '<' || c == '>' || c == '(' || c == ')' || c == INPUT_EOF;
}

static void scan_word(struct parser *p, struct token *token, struct strbuf *buffer) {
    for (;;) {
        int c = input_getc();
        if (metacharacter(c)) {
            input_ungetc(c);
            return;
        }
        if (c == '\\') {
            int next_char = input_getc();
            if (next_char == '\n') continue;
            token->quoted = 1;
            strbuf_putc(buffer, '\\');
            if (next_char == INPUT_EOF) return;
            strbuf_putc(buffer, (char)next_char);
            continue;
        }
        strbuf_putc(buffer, (char)c);
        if (c == '\'') {
            token->quoted = 1;
            scan_single_quoted(buffer, 0);
        } else if (c == '"') {
            token->quoted = 1;
            scan_double_quoted(p, buffer);
        } else if (c == '`') {
            scan_backquote(buffer);
        } else if (c == '$') {
            int after = input_peek();
            if (after == '\'' || after == '"') token->quoted = 1;
            scan_dollar(p, buffer);
        }
    }
}

static enum token_type operator_token(int c) {
    int n = input_getc();
    switch (c) {
    case '&':
        if (n == '&') return TOKEN_AND_IF;
        if (n == '>') {
            int m = input_getc();
            if (m == '>') return TOKEN_AND_DGREAT;
            input_ungetc(m);
            return TOKEN_AND_GREAT;
        }
        input_ungetc(n);
        return TOKEN_AMP;
    case '|':
        if (n == '|') return TOKEN_OR_IF;
        if (n == '&') return TOKEN_PIPE_AND;
        input_ungetc(n);
        return TOKEN_PIPE;
    case ';':
        if (n == ';') {
            int m = input_getc();
            if (m == '&') return TOKEN_SEMI_SEMI_AND;
            input_ungetc(m);
            return TOKEN_DSEMI;
        }
        if (n == '&') return TOKEN_SEMI_AND;
        input_ungetc(n);
        return TOKEN_SEMI;
    case '<':
        if (n == '<') {
            int m = input_getc();
            if (m == '-') return TOKEN_DLESSDASH;
            if (m == '<') return TOKEN_TLESS;
            input_ungetc(m);
            return TOKEN_DLESS;
        }
        if (n == '&') return TOKEN_LESSAND;
        if (n == '>') return TOKEN_LESSGREAT;
        input_ungetc(n);
        return TOKEN_LESS;
    case '>':
        if (n == '>') return TOKEN_DGREAT;
        if (n == '&') return TOKEN_GREATAND;
        if (n == '|') return TOKEN_CLOBBER;
        input_ungetc(n);
        return TOKEN_GREAT;
    case '(':
        input_ungetc(n);
        return TOKEN_LPAREN;
    default:
        input_ungetc(n);
        return TOKEN_RPAREN;
    }
}

static void lex(struct parser *p, struct token *token) {
    int c;
    for (;;) {
        c = input_getc();
        if (c == ' ' || c == '\t') continue;
        if (c == '\\') {
            int after = input_getc();
            if (after == '\n') continue;
            input_ungetc(after);
        }
        break;
    }
    token->line = input_line();
    token->quoted = 0;
    token->text = 0;
    if (c == '#') {
        do c = input_getc();
        while (c != '\n' && c != INPUT_EOF);
    }
    if (c == INPUT_EOF) {
        token->type = TOKEN_EOF;
        return;
    }
    if (c == '\n') {
        token->type = TOKEN_NEWLINE;
        token->line--;
        read_heredocs(p);
        return;
    }
    if (strchr("&|;<>()", c)) {
        if (c == '(' && p->command_position) {
            int after = input_getc();
            if (after == '(') {
                struct strbuf buffer;
                strbuf_init(&buffer, p->arena);
                scan_arithmetic(p, &buffer);
                strbuf_truncate(&buffer, buffer.length - 2);
                token->type = TOKEN_ARITH;
                token->text = strbuf_string(&buffer);
                return;
            }
            input_ungetc(after);
        }
        token->type = operator_token(c);
        return;
    }
    input_ungetc(c);
    struct strbuf buffer;
    strbuf_init(&buffer, p->arena);
    scan_word(p, token, &buffer);
    token->text = strbuf_string(&buffer);
    token->type = TOKEN_WORD;
    int after = input_peek();
    if ((after == '<' || after == '>') && !token->quoted) {
        int digits = 1;
        for (const char *s = token->text; *s; s++) digits &= isdigit((unsigned char)*s) != 0;
        if (digits) token->type = TOKEN_IO_NUMBER;
    }
}

static void next(struct parser *p) {
    for (;;) {
        lex(p, &p->token);
        int check = p->aliases && (p->command_position || p->alias_next);
        p->alias_next = 0;
        if (!check || p->token.type != TOKEN_WORD || p->token.quoted) return;
        struct alias *alias = alias_find(p->token.text);
        if (!alias || input_alias_active(alias)) return;
        input_push_alias(alias);
        size_t length = strlen(alias->value);
        p->alias_next = length && (alias->value[length - 1] == ' ' || alias->value[length - 1] == '\t');
    }
}

static int keyword(struct parser *p, const char *word) {
    return p->token.type == TOKEN_WORD && !p->token.quoted && !strcmp(p->token.text, word);
}

static int terminator(struct parser *p) {
    static const char *const words[] = {
        "then", "else", "elif", "fi", "do", "done", "esac", "}", 0,
    };
    switch (p->token.type) {
    case TOKEN_EOF: case TOKEN_RPAREN: case TOKEN_DSEMI: case TOKEN_SEMI_AND:
    case TOKEN_SEMI_SEMI_AND:
        return 1;
    case TOKEN_WORD:
        for (int i = 0; words[i]; i++) if (keyword(p, words[i])) return 1;
        return 0;
    default:
        return 0;
    }
}

static void command_start(struct parser *p) {
    p->command_position = 1;
    next(p);
    p->command_position = 0;
}

static void linebreak(struct parser *p) {
    while (p->token.type == TOKEN_NEWLINE) command_start(p);
}

static void expect_keyword(struct parser *p, const char *word) {
    if (!keyword(p, word)) unexpected(&p->token);
    command_start(p);
}

static struct word *make_word(struct parser *p, struct token *token) {
    struct word *word = arena_alloc(p->arena, sizeof(*word));
    word->next = 0;
    word->text = token->text;
    word->line = token->line;
    return word;
}

static char *unquote_delimiter(struct parser *p, const char *raw, int *quoted) {
    struct strbuf buffer;
    strbuf_init(&buffer, p->arena);
    *quoted = 0;
    for (const char *s = raw; *s; s++) {
        if (*s == '\\' && s[1]) {
            *quoted = 1;
            strbuf_putc(&buffer, *++s);
        } else if (*s == '\'' || *s == '"') {
            char quote = *s;
            *quoted = 1;
            for (s++; *s && *s != quote; s++) strbuf_putc(&buffer, *s);
            if (!*s) break;
        } else if (*s == '$' && (s[1] == '\'' || s[1] == '"')) {
            *quoted = 1;
        } else {
            strbuf_putc(&buffer, *s);
        }
    }
    return strbuf_string(&buffer);
}

static void read_heredocs(struct parser *p) {
    for (int i = 0; i < p->heredoc_count; i++) {
        struct redirect *redirect = p->heredocs[i];
        char *delimiter = redirect->heredoc;
        struct strbuf body;
        strbuf_init(&body, p->arena);
        input_prompt(2);
        for (;;) {
            struct strbuf line;
            strbuf_init(&line, p->arena);
            int c;
            while ((c = input_getc()) != INPUT_EOF && c != '\n') {
                if (redirect->heredoc_strip && c == '\t' && !line.length) continue;
                if (c == '\\' && !redirect->heredoc_quoted) {
                    int after = input_getc();
                    if (after == '\n') continue;
                    strbuf_putc(&line, '\\');
                    if (after == INPUT_EOF) break;
                    c = after;
                }
                strbuf_putc(&line, (char)c);
            }
            if (!strcmp(strbuf_string(&line), delimiter)) break;
            if (c == INPUT_EOF) {
                sh_warning("here-document delimited by end-of-file (wanted `%s')", delimiter);
                strbuf_append(&body, line.data ? line.data : "", line.length);
                break;
            }
            strbuf_append(&body, line.data ? line.data : "", line.length);
            strbuf_putc(&body, '\n');
        }
        redirect->heredoc = strbuf_string(&body);
    }
    p->heredoc_count = 0;
}

static int redirect_token(enum token_type type) {
    switch (type) {
    case TOKEN_LESS: case TOKEN_GREAT: case TOKEN_DLESS: case TOKEN_DLESSDASH:
    case TOKEN_TLESS: case TOKEN_DGREAT: case TOKEN_LESSAND: case TOKEN_GREATAND:
    case TOKEN_LESSGREAT: case TOKEN_CLOBBER: case TOKEN_AND_GREAT: case TOKEN_AND_DGREAT:
    case TOKEN_IO_NUMBER:
        return 1;
    default:
        return 0;
    }
}

static struct redirect *parse_redirect(struct parser *p) {
    struct redirect *redirect = arena_alloc(p->arena, sizeof(*redirect));
    memset(redirect, 0, sizeof(*redirect));
    redirect->fd = -1;
    if (p->token.type == TOKEN_IO_NUMBER) {
        redirect->fd = atoi(p->token.text);
        next(p);
    }
    enum token_type type = p->token.type;
    switch (type) {
    case TOKEN_LESS: redirect->type = REDIRECT_INPUT; break;
    case TOKEN_GREAT: redirect->type = REDIRECT_OUTPUT; break;
    case TOKEN_CLOBBER: redirect->type = REDIRECT_CLOBBER; break;
    case TOKEN_DGREAT: redirect->type = REDIRECT_APPEND; break;
    case TOKEN_LESSGREAT: redirect->type = REDIRECT_READ_WRITE; break;
    case TOKEN_LESSAND: redirect->type = REDIRECT_DUP_INPUT; break;
    case TOKEN_GREATAND: redirect->type = REDIRECT_DUP_OUTPUT; break;
    case TOKEN_DLESS: case TOKEN_DLESSDASH: redirect->type = REDIRECT_HEREDOC; break;
    case TOKEN_TLESS: redirect->type = REDIRECT_HERESTRING; break;
    case TOKEN_AND_GREAT: redirect->type = REDIRECT_OUTPUT_ERROR; break;
    case TOKEN_AND_DGREAT: redirect->type = REDIRECT_APPEND_ERROR; break;
    default: unexpected(&p->token);
    }
    if (redirect->fd < 0) {
        int input = type == TOKEN_LESS || type == TOKEN_LESSAND || type == TOKEN_LESSGREAT ||
                    type == TOKEN_DLESS || type == TOKEN_DLESSDASH || type == TOKEN_TLESS;
        redirect->fd = input ? 0 : 1;
    }
    next(p);
    if (p->token.type != TOKEN_WORD) unexpected(&p->token);
    redirect->target = make_word(p, &p->token);
    if (type == TOKEN_DLESS || type == TOKEN_DLESSDASH) {
        redirect->heredoc_strip = type == TOKEN_DLESSDASH;
        redirect->heredoc = unquote_delimiter(p, p->token.text, &redirect->heredoc_quoted);
        if (p->heredoc_count == (int)(sizeof(p->heredocs) / sizeof(p->heredocs[0])))
            sh_syntax_error("too many pending here-documents");
        p->heredocs[p->heredoc_count++] = redirect;
    }
    next(p);
    return redirect;
}

static void parse_redirect_list(struct parser *p, struct redirect **tail) {
    while (*tail) tail = &(*tail)->next;
    while (redirect_token(p->token.type)) {
        *tail = parse_redirect(p);
        tail = &(*tail)->next;
    }
}

static struct node *sequence(struct parser *p, struct node *left, struct node *right) {
    if (!left) return right;
    struct node *node = node_new(p->arena, NODE_SEQUENCE, left->line);
    node->binary.left = left;
    node->binary.right = right;
    return node;
}

static struct node *background(struct parser *p, struct node *body) {
    struct node *node = node_new(p->arena, NODE_BACKGROUND, body->line);
    node->unary.body = body;
    return node;
}

static struct node *parse_compound_list(struct parser *p) {
    struct node *list = 0;
    linebreak(p);
    while (!terminator(p)) {
        struct node *item = parse_and_or(p);
        if (p->token.type == TOKEN_AMP) item = background(p, item);
        list = sequence(p, list, item);
        if (p->token.type != TOKEN_SEMI && p->token.type != TOKEN_AMP &&
            p->token.type != TOKEN_NEWLINE)
            break;
        command_start(p);
        linebreak(p);
    }
    return list;
}

static struct node *require_list(struct parser *p) {
    struct node *list = parse_compound_list(p);
    if (!list) unexpected(&p->token);
    return list;
}

static struct node *parse_pipeline(struct parser *p) {
    int negate = 0;
    while (keyword(p, "!")) {
        negate = !negate;
        command_start(p);
    }
    struct node *first = parse_command(p);
    struct node *pipeline = first;
    if (p->token.type == TOKEN_PIPE || p->token.type == TOKEN_PIPE_AND) {
        int capacity = 4, count = 0, mask = 0;
        struct node **commands = arena_alloc(p->arena, sizeof(*commands) * (size_t)capacity);
        commands[count++] = first;
        while (p->token.type == TOKEN_PIPE || p->token.type == TOKEN_PIPE_AND) {
            if (p->token.type == TOKEN_PIPE_AND && count <= 30) mask |= 1 << (count - 1);
            command_start(p);
            linebreak(p);
            if (count == capacity) {
                struct node **grown = arena_alloc(p->arena, sizeof(*grown) * (size_t)capacity * 2);
                memcpy(grown, commands, sizeof(*commands) * (size_t)capacity);
                commands = grown;
                capacity *= 2;
            }
            commands[count++] = parse_command(p);
        }
        pipeline = node_new(p->arena, NODE_PIPELINE, first->line);
        pipeline->pipeline.commands = commands;
        pipeline->pipeline.count = count;
        pipeline->pipeline.stderr_mask = mask;
    }
    if (negate) {
        struct node *node = node_new(p->arena, NODE_NOT, pipeline->line);
        node->unary.body = pipeline;
        pipeline = node;
    }
    return pipeline;
}

static struct node *parse_and_or(struct parser *p) {
    struct node *left = parse_pipeline(p);
    while (p->token.type == TOKEN_AND_IF || p->token.type == TOKEN_OR_IF) {
        enum node_type type = p->token.type == TOKEN_AND_IF ? NODE_AND : NODE_OR;
        command_start(p);
        linebreak(p);
        struct node *node = node_new(p->arena, type, left->line);
        node->binary.left = left;
        node->binary.right = parse_pipeline(p);
        left = node;
    }
    return left;
}

static struct node *parse_if(struct parser *p, int line) {
    struct node *node = node_new(p->arena, NODE_IF, line);
    command_start(p);
    node->if_clause.condition = require_list(p);
    expect_keyword(p, "then");
    node->if_clause.then_part = parse_compound_list(p);
    if (keyword(p, "elif")) {
        node->if_clause.else_part = parse_if(p, p->token.line);
        return node;
    }
    if (keyword(p, "else")) {
        command_start(p);
        node->if_clause.else_part = parse_compound_list(p);
    }
    if (!keyword(p, "fi")) unexpected(&p->token);
    next(p);
    return node;
}

static struct node *parse_do_group(struct parser *p) {
    expect_keyword(p, "do");
    struct node *body = parse_compound_list(p);
    if (!keyword(p, "done")) unexpected(&p->token);
    next(p);
    return body;
}

static struct node *parse_loop(struct parser *p, enum node_type type, int line) {
    struct node *node = node_new(p->arena, type, line);
    command_start(p);
    node->loop.condition = require_list(p);
    node->loop.body = parse_do_group(p);
    return node;
}

static char *split_arith(struct parser *p, char **cursor) {
    char *start = *cursor;
    int depth = 0;
    char *s = start;
    for (; *s; s++) {
        if (*s == '(') depth++;
        else if (*s == ')') depth--;
        else if (*s == ';' && !depth) break;
    }
    char *part = arena_strndup(p->arena, start, (size_t)(s - start));
    *cursor = *s ? s + 1 : s;
    return part;
}

static struct node *parse_for(struct parser *p, int line) {
    p->command_position = 1;
    next(p);
    p->command_position = 0;
    if (p->token.type == TOKEN_ARITH) {
        struct node *node = node_new(p->arena, NODE_ARITH_FOR, line);
        char *cursor = p->token.text;
        node->arith_for.init = split_arith(p, &cursor);
        node->arith_for.condition = split_arith(p, &cursor);
        node->arith_for.step = split_arith(p, &cursor);
        command_start(p);
        if (p->token.type == TOKEN_SEMI) command_start(p);
        linebreak(p);
        if (keyword(p, "{")) {
            command_start(p);
            node->arith_for.body = parse_compound_list(p);
            if (!keyword(p, "}")) unexpected(&p->token);
            next(p);
        } else {
            node->arith_for.body = parse_do_group(p);
        }
        return node;
    }
    if (p->token.type != TOKEN_WORD || !is_name(p->token.text))
        sh_syntax_error("`%s': not a valid identifier",
                        p->token.text ? p->token.text : token_names[p->token.type]);
    struct node *node = node_new(p->arena, NODE_FOR, line);
    node->for_clause.name = p->token.text;
    command_start(p);
    linebreak(p);
    if (keyword(p, "in")) {
        node->for_clause.has_items = 1;
        struct word **tail = &node->for_clause.items;
        next(p);
        while (p->token.type == TOKEN_WORD) {
            *tail = make_word(p, &p->token);
            tail = &(*tail)->next;
            next(p);
        }
        if (p->token.type != TOKEN_SEMI && p->token.type != TOKEN_NEWLINE)
            unexpected(&p->token);
        command_start(p);
        linebreak(p);
    } else if (p->token.type == TOKEN_SEMI) {
        command_start(p);
        linebreak(p);
    }
    node->for_clause.body = parse_do_group(p);
    return node;
}

static struct node *parse_case(struct parser *p, int line) {
    struct node *node = node_new(p->arena, NODE_CASE, line);
    next(p);
    if (p->token.type != TOKEN_WORD) unexpected(&p->token);
    node->case_clause.subject = make_word(p, &p->token);
    next(p);
    while (p->token.type == TOKEN_NEWLINE) next(p);
    if (!keyword(p, "in")) unexpected(&p->token);
    next(p);
    while (p->token.type == TOKEN_NEWLINE) next(p);
    struct case_item **tail = &node->case_clause.items;
    while (!keyword(p, "esac")) {
        struct case_item *item = arena_alloc(p->arena, sizeof(*item));
        memset(item, 0, sizeof(*item));
        if (p->token.type == TOKEN_LPAREN) next(p);
        struct word **patterns = &item->patterns;
        for (;;) {
            if (p->token.type != TOKEN_WORD) unexpected(&p->token);
            *patterns = make_word(p, &p->token);
            patterns = &(*patterns)->next;
            next(p);
            if (p->token.type != TOKEN_PIPE) break;
            next(p);
        }
        if (p->token.type != TOKEN_RPAREN) unexpected(&p->token);
        command_start(p);
        item->body = parse_compound_list(p);
        *tail = item;
        tail = &item->next;
        if (p->token.type == TOKEN_DSEMI || p->token.type == TOKEN_SEMI_AND ||
            p->token.type == TOKEN_SEMI_SEMI_AND) {
            item->terminator = p->token.type == TOKEN_SEMI_AND ? CASE_FALLTHROUGH :
                               p->token.type == TOKEN_SEMI_SEMI_AND ? CASE_CONTINUE : CASE_BREAK;
            next(p);
            while (p->token.type == TOKEN_NEWLINE) next(p);
            continue;
        }
        if (!keyword(p, "esac")) unexpected(&p->token);
    }
    next(p);
    return node;
}

static struct word *cond_word(struct parser *p) {
    if (p->token.type != TOKEN_WORD) unexpected(&p->token);
    struct word *word = make_word(p, &p->token);
    next(p);
    return word;
}

static struct cond *cond_node(struct parser *p, enum cond_type type) {
    struct cond *cond = arena_alloc(p->arena, sizeof(*cond));
    memset(cond, 0, sizeof(*cond));
    cond->type = type;
    return cond;
}

static struct cond *parse_cond_or(struct parser *p);

static int cond_unary_operator(const char *text) {
    return text[0] == '-' && text[1] && !text[2] && strchr("abcdefghknoprstuvwxzGLNORS", text[1]);
}

static int cond_binary_operator(const char *text) {
    static const char *const ops[] = {
        "=", "==", "!=", "=~", "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef", 0,
    };
    for (int i = 0; ops[i]; i++) if (!strcmp(text, ops[i])) return 1;
    return 0;
}

static struct cond *parse_cond_primary(struct parser *p) {
    while (p->token.type == TOKEN_NEWLINE) next(p);
    if (p->token.type == TOKEN_LPAREN) {
        next(p);
        struct cond *inner = parse_cond_or(p);
        if (p->token.type != TOKEN_RPAREN) unexpected(&p->token);
        next(p);
        return inner;
    }
    if (keyword(p, "!")) {
        next(p);
        struct cond *cond = cond_node(p, COND_NOT);
        cond->first = parse_cond_primary(p);
        return cond;
    }
    if (p->token.type == TOKEN_WORD && !p->token.quoted && cond_unary_operator(p->token.text)) {
        struct cond *cond = cond_node(p, COND_UNARY);
        cond->op = p->token.text;
        next(p);
        if (p->token.type == TOKEN_WORD && strcmp(p->token.text, "]]")) {
            cond->left = cond_word(p);
            return cond;
        }
        cond->type = COND_WORD;
        cond->left = word_new(p->arena, cond->op, p->token.line);
        cond->op = 0;
        return cond;
    }
    struct word *left = cond_word(p);
    const char *op = 0;
    if (p->token.type == TOKEN_WORD && !p->token.quoted && cond_binary_operator(p->token.text))
        op = p->token.text;
    else if (p->token.type == TOKEN_LESS)
        op = "<";
    else if (p->token.type == TOKEN_GREAT)
        op = ">";
    if (!op) {
        struct cond *cond = cond_node(p, COND_WORD);
        cond->left = left;
        return cond;
    }
    struct cond *cond = cond_node(p, COND_BINARY);
    cond->op = arena_strdup(p->arena, op);
    cond->left = left;
    next(p);
    cond->right = cond_word(p);
    return cond;
}

static struct cond *parse_cond_and(struct parser *p) {
    struct cond *left = parse_cond_primary(p);
    while (p->token.type == TOKEN_AND_IF) {
        next(p);
        struct cond *cond = cond_node(p, COND_AND);
        cond->first = left;
        cond->second = parse_cond_primary(p);
        left = cond;
    }
    return left;
}

static struct cond *parse_cond_or(struct parser *p) {
    struct cond *left = parse_cond_and(p);
    while (p->token.type == TOKEN_OR_IF) {
        next(p);
        struct cond *cond = cond_node(p, COND_OR);
        cond->first = left;
        cond->second = parse_cond_and(p);
        left = cond;
    }
    return left;
}

static struct node *parse_cond(struct parser *p, int line) {
    struct node *node = node_new(p->arena, NODE_COND, line);
    next(p);
    node->cond.expression = parse_cond_or(p);
    if (!keyword(p, "]]")) unexpected(&p->token);
    next(p);
    return node;
}

static struct node *parse_function_body(struct parser *p, const char *name, int line) {
    linebreak(p);
    struct node *node = node_new(p->arena, NODE_FUNCTION, line);
    node->function.name = arena_strdup(p->arena, name);
    struct node *body = parse_command(p);
    if (body->type == NODE_SIMPLE || body->type == NODE_FUNCTION)
        sh_syntax_error("function body must be a compound command");
    node->function.body = body;
    return node;
}

static struct node *parse_function_keyword(struct parser *p, int line) {
    next(p);
    if (p->token.type != TOKEN_WORD) unexpected(&p->token);
    char *name = p->token.text;
    command_start(p);
    if (p->token.type == TOKEN_LPAREN) {
        next(p);
        if (p->token.type != TOKEN_RPAREN) unexpected(&p->token);
        command_start(p);
    }
    return parse_function_body(p, name, line);
}

static struct node *parse_compound(struct parser *p) {
    int line = p->token.line;
    struct node *node = 0;
    if (p->token.type == TOKEN_LPAREN) {
        command_start(p);
        node = node_new(p->arena, NODE_SUBSHELL, line);
        node->unary.body = require_list(p);
        if (p->token.type != TOKEN_RPAREN) unexpected(&p->token);
        next(p);
    } else if (p->token.type == TOKEN_ARITH) {
        node = node_new(p->arena, NODE_ARITH, line);
        node->arith.expression = p->token.text;
        next(p);
    } else if (keyword(p, "{")) {
        command_start(p);
        node = node_new(p->arena, NODE_GROUP, line);
        node->unary.body = require_list(p);
        if (!keyword(p, "}")) unexpected(&p->token);
        next(p);
    } else if (keyword(p, "if")) {
        node = parse_if(p, line);
    } else if (keyword(p, "while")) {
        node = parse_loop(p, NODE_WHILE, line);
    } else if (keyword(p, "until")) {
        node = parse_loop(p, NODE_UNTIL, line);
    } else if (keyword(p, "for")) {
        node = parse_for(p, line);
    } else if (keyword(p, "case")) {
        node = parse_case(p, line);
    } else if (keyword(p, "[[")) {
        node = parse_cond(p, line);
    } else {
        return 0;
    }
    parse_redirect_list(p, &node->redirects);
    return node;
}

static struct node *parse_simple(struct parser *p) {
    struct node *node = node_new(p->arena, NODE_SIMPLE, p->token.line);
    struct word **assignments = &node->simple.assignments;
    struct word **words = &node->simple.words;
    struct redirect **redirects = &node->redirects;
    for (;;) {
        if (redirect_token(p->token.type)) {
            *redirects = parse_redirect(p);
            redirects = &(*redirects)->next;
            continue;
        }
        if (p->token.type != TOKEN_WORD) break;
        if (!node->simple.words && assignment_name_length(p->token.text)) {
            *assignments = make_word(p, &p->token);
            assignments = &(*assignments)->next;
            p->command_position = 1;
            next(p);
            p->command_position = 0;
            continue;
        }
        int first = !node->simple.words;
        *words = make_word(p, &p->token);
        words = &(*words)->next;
        if (first && !node->simple.assignments && !node->redirects && !p->token.quoted &&
            is_name(p->token.text)) {
            char *name = p->token.text;
            next(p);
            if (p->token.type == TOKEN_LPAREN) {
                next(p);
                if (p->token.type != TOKEN_RPAREN) unexpected(&p->token);
                command_start(p);
                return parse_function_body(p, name, node->line);
            }
            continue;
        }
        next(p);
    }
    if (!node->simple.words && !node->simple.assignments && !node->redirects)
        unexpected(&p->token);
    return node;
}

static struct node *parse_command(struct parser *p) {
    if (keyword(p, "function")) return parse_function_keyword(p, p->token.line);
    struct node *compound = parse_compound(p);
    if (compound) return compound;
    if (terminator(p) || (p->token.type != TOKEN_WORD && !redirect_token(p->token.type)))
        unexpected(&p->token);
    return parse_simple(p);
}

enum parse_result parse_command_line(struct arena *arena, struct node **result) {
    struct parser p;
    parser_init(&p, arena);
    *result = 0;
    input_prompt(1);
    command_start(&p);
    if (p.token.type == TOKEN_EOF) return PARSE_EOF;
    if (p.token.type == TOKEN_NEWLINE) return PARSE_EMPTY;
    struct node *list = 0;
    for (;;) {
        struct node *item = parse_and_or(&p);
        if (p.token.type == TOKEN_AMP) item = background(&p, item);
        list = sequence(&p, list, item);
        if (p.token.type == TOKEN_SEMI || p.token.type == TOKEN_AMP) {
            command_start(&p);
            if (p.token.type == TOKEN_NEWLINE || p.token.type == TOKEN_EOF) break;
            continue;
        }
        if (p.token.type == TOKEN_NEWLINE || p.token.type == TOKEN_EOF) break;
        unexpected(&p.token);
    }
    if (p.token.type == TOKEN_EOF && p.heredoc_count) read_heredocs(&p);
    *result = list;
    return PARSE_COMMAND;
}

struct node *parse_string(struct arena *arena, const char *text, const char *name) {
    int level = input_level();
    input_push_string(text, name);
    struct node *program = 0;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) {
        for (;;) {
            struct node *node;
            enum parse_result result = parse_command_line(arena, &node);
            if (result == PARSE_EOF) break;
            if (result == PARSE_COMMAND) {
                struct parser p;
                parser_init(&p, arena);
                program = sequence(&p, program, node);
            }
        }
    }
    END_TRY(&frame);
    input_unwind(level);
    if (kind) raise_exception(kind);
    return program;
}
