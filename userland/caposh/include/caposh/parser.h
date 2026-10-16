#pragma once

#include <caposh/ast.h>

enum parse_result {
    PARSE_COMMAND,
    PARSE_EMPTY,
    PARSE_EOF,
};

enum parse_result parse_command_line(struct arena *arena, struct node **result);
struct node *parse_string(struct arena *arena, const char *text, const char *name);
int is_name(const char *text);
int is_name_prefix(const char *text, size_t length);
size_t assignment_name_length(const char *text);
