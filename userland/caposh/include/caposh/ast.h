#pragma once

#include <caposh/memory.h>

enum node_type {
    NODE_SIMPLE,
    NODE_PIPELINE,
    NODE_AND,
    NODE_OR,
    NODE_SEQUENCE,
    NODE_BACKGROUND,
    NODE_NOT,
    NODE_SUBSHELL,
    NODE_GROUP,
    NODE_IF,
    NODE_WHILE,
    NODE_UNTIL,
    NODE_FOR,
    NODE_ARITH_FOR,
    NODE_CASE,
    NODE_FUNCTION,
    NODE_ARITH,
    NODE_COND,
};

enum redirect_type {
    REDIRECT_INPUT,
    REDIRECT_OUTPUT,
    REDIRECT_CLOBBER,
    REDIRECT_APPEND,
    REDIRECT_READ_WRITE,
    REDIRECT_DUP_INPUT,
    REDIRECT_DUP_OUTPUT,
    REDIRECT_HEREDOC,
    REDIRECT_HERESTRING,
    REDIRECT_OUTPUT_ERROR,
    REDIRECT_APPEND_ERROR,
};

enum case_terminator { CASE_BREAK, CASE_FALLTHROUGH, CASE_CONTINUE };

enum cond_type {
    COND_WORD,
    COND_UNARY,
    COND_BINARY,
    COND_NOT,
    COND_AND,
    COND_OR,
};

struct word {
    struct word *next;
    char *text;
    int line;
};

struct redirect {
    struct redirect *next;
    enum redirect_type type;
    int fd;
    struct word *target;
    char *heredoc;
    int heredoc_quoted;
    int heredoc_strip;
};

struct case_item {
    struct case_item *next;
    struct word *patterns;
    struct node *body;
    enum case_terminator terminator;
};

struct cond {
    enum cond_type type;
    char *op;
    struct word *left, *right;
    struct cond *first, *second;
};

struct node {
    enum node_type type;
    int line;
    struct redirect *redirects;
    union {
        struct {
            struct word *assignments;
            struct word *words;
        } simple;
        struct {
            struct node **commands;
            int count;
            int stderr_mask;
        } pipeline;
        struct {
            struct node *left, *right;
        } binary;
        struct {
            struct node *body;
        } unary;
        struct {
            struct node *condition, *then_part, *else_part;
        } if_clause;
        struct {
            struct node *condition, *body;
        } loop;
        struct {
            char *name;
            struct word *items;
            int has_items;
            struct node *body;
        } for_clause;
        struct {
            char *init, *condition, *step;
            struct node *body;
        } arith_for;
        struct {
            struct word *subject;
            struct case_item *items;
        } case_clause;
        struct {
            char *name;
            struct node *body;
            char *source;
        } function;
        struct {
            char *expression;
        } arith;
        struct {
            struct cond *expression;
        } cond;
    };
};

struct node *node_new(struct arena *arena, enum node_type type, int line);
struct word *word_new(struct arena *arena, const char *text, int line);
struct node *node_copy(struct arena *arena, const struct node *node);
struct word *word_copy(struct arena *arena, const struct word *word);
