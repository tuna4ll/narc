#pragma once

#include <caposh/strbuf.h>
#include <stddef.h>

#define INPUT_EOF (-1)

struct alias;

void input_push_string(const char *text, const char *name);
void input_push_file(int fd, const char *name);
void input_push_interactive(void);
void input_push_alias(struct alias *alias);
void input_pop(void);
int input_depth(void);
int input_level(void);
void input_unwind(int level);

int input_getc(void);
void input_ungetc(int c);
int input_peek(void);
void input_discard_line(void);

int input_line(void);
void input_set_line(int line);
const char *input_name(void);
int input_interactive(void);
int input_alias_active(const struct alias *alias);
int input_at_alias_end_with_space(void);

void input_prompt(int level);
void input_capture_begin(struct strbuf *buffer);
void input_capture_end(void);
void input_sync(void);
