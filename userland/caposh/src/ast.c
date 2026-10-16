#include <caposh/ast.h>
#include <string.h>

struct node *node_new(struct arena *arena, enum node_type type, int line) {
    struct node *node = arena_alloc(arena, sizeof(*node));
    memset(node, 0, sizeof(*node));
    node->type = type;
    node->line = line;
    return node;
}

struct word *word_new(struct arena *arena, const char *text, int line) {
    struct word *word = arena_alloc(arena, sizeof(*word));
    word->next = 0;
    word->text = arena_strdup(arena, text);
    word->line = line;
    return word;
}

static char *copy_string(struct arena *arena, const char *string) {
    return string ? arena_strdup(arena, string) : 0;
}

struct word *word_copy(struct arena *arena, const struct word *word) {
    struct word *head = 0, **tail = &head;
    for (; word; word = word->next) {
        *tail = word_new(arena, word->text, word->line);
        tail = &(*tail)->next;
    }
    return head;
}

static struct redirect *redirect_copy(struct arena *arena, const struct redirect *redirect) {
    struct redirect *head = 0, **tail = &head;
    for (; redirect; redirect = redirect->next) {
        struct redirect *copy = arena_alloc(arena, sizeof(*copy));
        *copy = *redirect;
        copy->next = 0;
        copy->target = word_copy(arena, redirect->target);
        copy->heredoc = copy_string(arena, redirect->heredoc);
        *tail = copy;
        tail = &copy->next;
    }
    return head;
}

static struct cond *cond_copy(struct arena *arena, const struct cond *cond) {
    if (!cond) return 0;
    struct cond *copy = arena_alloc(arena, sizeof(*copy));
    copy->type = cond->type;
    copy->op = copy_string(arena, cond->op);
    copy->left = word_copy(arena, cond->left);
    copy->right = word_copy(arena, cond->right);
    copy->first = cond_copy(arena, cond->first);
    copy->second = cond_copy(arena, cond->second);
    return copy;
}

static struct case_item *case_copy(struct arena *arena, const struct case_item *item) {
    struct case_item *head = 0, **tail = &head;
    for (; item; item = item->next) {
        struct case_item *copy = arena_alloc(arena, sizeof(*copy));
        copy->next = 0;
        copy->patterns = word_copy(arena, item->patterns);
        copy->body = node_copy(arena, item->body);
        copy->terminator = item->terminator;
        *tail = copy;
        tail = &copy->next;
    }
    return head;
}

struct node *node_copy(struct arena *arena, const struct node *node) {
    if (!node) return 0;
    struct node *copy = node_new(arena, node->type, node->line);
    copy->redirects = redirect_copy(arena, node->redirects);
    switch (node->type) {
    case NODE_SIMPLE:
        copy->simple.assignments = word_copy(arena, node->simple.assignments);
        copy->simple.words = word_copy(arena, node->simple.words);
        break;
    case NODE_PIPELINE:
        copy->pipeline.count = node->pipeline.count;
        copy->pipeline.stderr_mask = node->pipeline.stderr_mask;
        copy->pipeline.commands = arena_alloc(arena, sizeof(struct node *) * (size_t)node->pipeline.count);
        for (int i = 0; i < node->pipeline.count; i++)
            copy->pipeline.commands[i] = node_copy(arena, node->pipeline.commands[i]);
        break;
    case NODE_AND:
    case NODE_OR:
    case NODE_SEQUENCE:
        copy->binary.left = node_copy(arena, node->binary.left);
        copy->binary.right = node_copy(arena, node->binary.right);
        break;
    case NODE_BACKGROUND:
    case NODE_NOT:
    case NODE_SUBSHELL:
    case NODE_GROUP:
        copy->unary.body = node_copy(arena, node->unary.body);
        break;
    case NODE_IF:
        copy->if_clause.condition = node_copy(arena, node->if_clause.condition);
        copy->if_clause.then_part = node_copy(arena, node->if_clause.then_part);
        copy->if_clause.else_part = node_copy(arena, node->if_clause.else_part);
        break;
    case NODE_WHILE:
    case NODE_UNTIL:
        copy->loop.condition = node_copy(arena, node->loop.condition);
        copy->loop.body = node_copy(arena, node->loop.body);
        break;
    case NODE_FOR:
        copy->for_clause.name = copy_string(arena, node->for_clause.name);
        copy->for_clause.items = word_copy(arena, node->for_clause.items);
        copy->for_clause.has_items = node->for_clause.has_items;
        copy->for_clause.body = node_copy(arena, node->for_clause.body);
        break;
    case NODE_ARITH_FOR:
        copy->arith_for.init = copy_string(arena, node->arith_for.init);
        copy->arith_for.condition = copy_string(arena, node->arith_for.condition);
        copy->arith_for.step = copy_string(arena, node->arith_for.step);
        copy->arith_for.body = node_copy(arena, node->arith_for.body);
        break;
    case NODE_CASE:
        copy->case_clause.subject = word_copy(arena, node->case_clause.subject);
        copy->case_clause.items = case_copy(arena, node->case_clause.items);
        break;
    case NODE_FUNCTION:
        copy->function.name = copy_string(arena, node->function.name);
        copy->function.body = node_copy(arena, node->function.body);
        copy->function.source = copy_string(arena, node->function.source);
        break;
    case NODE_ARITH:
        copy->arith.expression = copy_string(arena, node->arith.expression);
        break;
    case NODE_COND:
        copy->cond.expression = cond_copy(arena, node->cond.expression);
        break;
    }
    return copy;
}
