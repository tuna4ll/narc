#pragma once

#include <caposh/ast.h>

#define EXPAND_TILDE  0x01
#define EXPAND_ASSIGN 0x02
#define EXPAND_NOGLOB 0x04
#define EXPAND_NOSPLIT 0x08

char **expand_words(struct word *words, int *count);
char *expand_string(const char *text, int flags);
char *expand_assignment(const char *text);
char *expand_pattern(const char *text);
char *expand_heredoc(const char *body);
char *expand_single(struct word *word, const char *context);
char *expand_prompt_parameters(const char *text);
char *expand_ansi_c(const char *text, size_t length);
const char *skip_substitution(const char *text);
const char *skip_braced(const char *text);
const char *skip_arithmetic(const char *text);
const char *skip_backquote(const char *text);
