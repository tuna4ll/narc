#include <caposh/printer.h>
#include <caposh/strbuf.h>
#include <string.h>

struct printer {
    struct strbuf out;
    int multiline;
    int indent;
};

static void node(struct printer *p, const struct node *n);

static void newline(struct printer *p) {
    if (!p->multiline) {
        strbuf_puts(&p->out, "; ");
        return;
    }
    strbuf_putc(&p->out, '\n');
    for (int i = 0; i < p->indent; i++) strbuf_puts(&p->out, "    ");
}

static void words(struct printer *p, const struct word *word, int leading_space) {
    for (; word; word = word->next) {
        if (leading_space) strbuf_putc(&p->out, ' ');
        strbuf_puts(&p->out, word->text);
        leading_space = 1;
    }
}

static void redirects(struct printer *p, const struct redirect *r) {
    static const char *const operators[] = {
        [REDIRECT_INPUT] = "<", [REDIRECT_OUTPUT] = ">", [REDIRECT_CLOBBER] = ">|",
        [REDIRECT_APPEND] = ">>", [REDIRECT_READ_WRITE] = "<>", [REDIRECT_DUP_INPUT] = "<&",
        [REDIRECT_DUP_OUTPUT] = ">&", [REDIRECT_HEREDOC] = "<<", [REDIRECT_HERESTRING] = "<<<",
        [REDIRECT_OUTPUT_ERROR] = "&>", [REDIRECT_APPEND_ERROR] = "&>>",
    };
    for (; r; r = r->next) {
        strbuf_putc(&p->out, ' ');
        int input = r->type == REDIRECT_INPUT || r->type == REDIRECT_DUP_INPUT ||
                    r->type == REDIRECT_READ_WRITE || r->type == REDIRECT_HEREDOC ||
                    r->type == REDIRECT_HERESTRING;
        int error_form = r->type == REDIRECT_OUTPUT_ERROR || r->type == REDIRECT_APPEND_ERROR;
        if (!error_form && r->fd != (input ? 0 : 1)) strbuf_printf(&p->out, "%d", r->fd);
        strbuf_puts(&p->out, operators[r->type]);
        if (r->type == REDIRECT_HEREDOC && r->heredoc_strip) strbuf_putc(&p->out, '-');
        strbuf_puts(&p->out, r->target->text);
    }
}

static void heredoc_bodies(struct printer *p, const struct redirect *r) {
    for (; r; r = r->next) {
        if (r->type != REDIRECT_HEREDOC || !p->multiline) continue;
        strbuf_putc(&p->out, '\n');
        strbuf_puts(&p->out, r->heredoc ? r->heredoc : "");
        const char *delimiter = r->target->text;
        struct strbuf plain;
        strbuf_init(&plain, stack);
        for (const char *s = delimiter; *s; s++)
            if (*s != '\'' && *s != '"' && *s != '\\') strbuf_putc(&plain, *s);
        strbuf_puts(&p->out, strbuf_string(&plain));
    }
}

static void body(struct printer *p, const struct node *n) {
    p->indent++;
    newline(p);
    node(p, n);
    p->indent--;
    newline(p);
}

static void cond(struct printer *p, const struct cond *c) {
    switch (c->type) {
    case COND_WORD: words(p, c->left, 0); break;
    case COND_UNARY:
        strbuf_puts(&p->out, c->op);
        words(p, c->left, 1);
        break;
    case COND_BINARY:
        words(p, c->left, 0);
        strbuf_printf(&p->out, " %s ", c->op);
        words(p, c->right, 0);
        break;
    case COND_NOT:
        strbuf_puts(&p->out, "! ");
        cond(p, c->first);
        break;
    case COND_AND:
    case COND_OR:
        cond(p, c->first);
        strbuf_puts(&p->out, c->type == COND_AND ? " && " : " || ");
        cond(p, c->second);
        break;
    }
}

static void node(struct printer *p, const struct node *n) {
    if (!n) return;
    switch (n->type) {
    case NODE_SIMPLE: {
        int space = 0;
        for (const struct word *w = n->simple.assignments; w; w = w->next) {
            if (space) strbuf_putc(&p->out, ' ');
            strbuf_puts(&p->out, w->text);
            space = 1;
        }
        words(p, n->simple.words, space);
        redirects(p, n->redirects);
        heredoc_bodies(p, n->redirects);
        return;
    }
    case NODE_PIPELINE:
        for (int i = 0; i < n->pipeline.count; i++) {
            if (i) strbuf_puts(&p->out, (n->pipeline.stderr_mask & (1 << (i - 1))) ? " |& " : " | ");
            node(p, n->pipeline.commands[i]);
        }
        return;
    case NODE_AND:
    case NODE_OR:
        node(p, n->binary.left);
        strbuf_puts(&p->out, n->type == NODE_AND ? " && " : " || ");
        node(p, n->binary.right);
        return;
    case NODE_SEQUENCE:
        node(p, n->binary.left);
        if (n->binary.left && n->binary.left->type == NODE_BACKGROUND) {
            if (p->multiline) newline(p);
            else strbuf_putc(&p->out, ' ');
        } else {
            newline(p);
        }
        node(p, n->binary.right);
        return;
    case NODE_BACKGROUND:
        node(p, n->unary.body);
        strbuf_puts(&p->out, " &");
        return;
    case NODE_NOT:
        strbuf_puts(&p->out, "! ");
        node(p, n->unary.body);
        return;
    case NODE_SUBSHELL:
        strbuf_puts(&p->out, "( ");
        node(p, n->unary.body);
        strbuf_puts(&p->out, " )");
        break;
    case NODE_GROUP:
        strbuf_putc(&p->out, '{');
        body(p, n->unary.body);
        strbuf_putc(&p->out, '}');
        break;
    case NODE_IF: {
        strbuf_puts(&p->out, "if ");
        node(p, n->if_clause.condition);
        strbuf_puts(&p->out, "; then");
        body(p, n->if_clause.then_part);
        const struct node *rest = n->if_clause.else_part;
        while (rest && rest->type == NODE_IF && !rest->redirects) {
            strbuf_puts(&p->out, "elif ");
            node(p, rest->if_clause.condition);
            strbuf_puts(&p->out, "; then");
            body(p, rest->if_clause.then_part);
            rest = rest->if_clause.else_part;
        }
        if (rest) {
            strbuf_puts(&p->out, "else");
            body(p, rest);
        }
        strbuf_puts(&p->out, "fi");
        break;
    }
    case NODE_WHILE:
    case NODE_UNTIL:
        strbuf_puts(&p->out, n->type == NODE_WHILE ? "while " : "until ");
        node(p, n->loop.condition);
        strbuf_puts(&p->out, "; do");
        body(p, n->loop.body);
        strbuf_puts(&p->out, "done");
        break;
    case NODE_FOR:
        strbuf_printf(&p->out, "for %s", n->for_clause.name);
        if (n->for_clause.has_items) {
            strbuf_puts(&p->out, " in");
            words(p, n->for_clause.items, 1);
        }
        strbuf_puts(&p->out, "; do");
        body(p, n->for_clause.body);
        strbuf_puts(&p->out, "done");
        break;
    case NODE_ARITH_FOR:
        strbuf_printf(&p->out, "for ((%s; %s; %s)); do", n->arith_for.init ? n->arith_for.init : "",
                      n->arith_for.condition ? n->arith_for.condition : "",
                      n->arith_for.step ? n->arith_for.step : "");
        body(p, n->arith_for.body);
        strbuf_puts(&p->out, "done");
        break;
    case NODE_CASE:
        strbuf_puts(&p->out, "case ");
        strbuf_puts(&p->out, n->case_clause.subject->text);
        strbuf_puts(&p->out, " in");
        p->indent++;
        for (const struct case_item *item = n->case_clause.items; item; item = item->next) {
            newline(p);
            for (const struct word *w = item->patterns; w; w = w->next) {
                strbuf_puts(&p->out, w->text);
                if (w->next) strbuf_puts(&p->out, " | ");
            }
            strbuf_putc(&p->out, ')');
            if (item->body) {
                p->indent++;
                newline(p);
                node(p, item->body);
                p->indent--;
            }
            newline(p);
            strbuf_puts(&p->out, item->terminator == CASE_FALLTHROUGH ? ";&" :
                                 item->terminator == CASE_CONTINUE ? ";;&" : ";;");
        }
        p->indent--;
        newline(p);
        strbuf_puts(&p->out, "esac");
        break;
    case NODE_FUNCTION:
        strbuf_printf(&p->out, "%s () ", n->function.name);
        node(p, n->function.body);
        return;
    case NODE_ARITH:
        strbuf_printf(&p->out, "((%s))", n->arith.expression);
        break;
    case NODE_COND:
        strbuf_puts(&p->out, "[[ ");
        cond(p, n->cond.expression);
        strbuf_puts(&p->out, " ]]");
        break;
    }
    redirects(p, n->redirects);
}

char *print_text(const struct node *n) {
    struct printer p;
    strbuf_init(&p.out, stack);
    p.multiline = 0;
    p.indent = 0;
    node(&p, n);
    return strbuf_string(&p.out);
}

char *print_source(const struct node *n, int indent) {
    struct printer p;
    strbuf_init(&p.out, stack);
    p.multiline = 1;
    p.indent = indent;
    node(&p, n);
    return strbuf_string(&p.out);
}
