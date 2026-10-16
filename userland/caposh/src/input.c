#include <caposh/alias.h>
#include <caposh/history.h>
#include <caposh/input.h>
#include <caposh/lineedit.h>
#include <caposh/options.h>
#include <caposh/prompt.h>
#include <caposh/shell.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FILE_BUFFER 4096
#define PUSHBACK 8
#define CAPTURE_DEPTH 16

enum input_kind { INPUT_STRING, INPUT_FILE, INPUT_INTERACTIVE, INPUT_ALIAS };

struct input {
    enum input_kind kind;
    struct input *previous;
    char *name;
    int line;
    char *text;
    size_t length, position;
    int fd;
    char *buffer;
    size_t buffer_position, buffer_length;
    int buffered;
    int eof;
    struct alias *alias;
    int pushback[PUSHBACK];
    int pushback_count;
};

static struct input *current;
static int depth;
static int prompt_level = 1;
static struct strbuf *captures[CAPTURE_DEPTH];
static int capture_count;

static struct input *push(enum input_kind kind, const char *name) {
    struct input *input = xcalloc(1, sizeof(*input));
    input->kind = kind;
    input->previous = current;
    input->name = name ? xstrdup(name) : current && current->name ? xstrdup(current->name) : 0;
    input->line = kind == INPUT_ALIAS && current ? current->line : 1;
    input->fd = -1;
    current = input;
    depth++;
    return input;
}

void input_push_string(const char *text, const char *name) {
    struct input *input = push(INPUT_STRING, name);
    input->length = strlen(text);
    input->text = xstrndup(text, input->length);
    if (!name && input->previous) input->line = input->previous->line;
}

void input_push_file(int fd, const char *name) {
    struct input *input = push(INPUT_FILE, name);
    input->fd = fd;
    input->buffered = fd != 0 || lseek(fd, 0, SEEK_CUR) >= 0;
    input->buffer = xmalloc(FILE_BUFFER);
}

void input_push_interactive(void) {
    push(INPUT_INTERACTIVE, 0);
}

void input_push_alias(struct alias *alias) {
    struct input *input = push(INPUT_ALIAS, 0);
    input->alias = alias;
    input->text = xstrdup(alias->value);
    input->length = strlen(input->text);
    alias->active++;
}

void input_pop(void) {
    struct input *input = current;
    if (!input) return;
    if (input->kind == INPUT_ALIAS) {
        input->alias->active--;
        if (input->previous) input->previous->line = input->line;
    }
    current = input->previous;
    depth--;
    free(input->name);
    free(input->text);
    free(input->buffer);
    free(input);
}

int input_level(void) {
    return depth;
}

void input_unwind(int level) {
    while (depth > level) input_pop();
}

int input_depth(void) {
    int count = 0;
    for (struct input *input = current; input; input = input->previous)
        if (input->kind != INPUT_ALIAS) count++;
    return count;
}

static int read_file(struct input *input) {
    if (input->eof) return INPUT_EOF;
    if (input->buffer_position < input->buffer_length)
        return (unsigned char)input->buffer[input->buffer_position++];
    for (;;) {
        ssize_t result = read(input->fd, input->buffer, input->buffered ? FILE_BUFFER : 1);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            input->eof = 1;
            return INPUT_EOF;
        }
        input->buffer_position = 1;
        input->buffer_length = (size_t)result;
        if (option_enabled(OPTION_VERBOSE)) fwrite(input->buffer, 1, (size_t)result, stderr);
        return (unsigned char)input->buffer[0];
    }
}

static int read_interactive(struct input *input) {
    if (input->position < input->length) return (unsigned char)input->text[input->position++];
    if (input->eof) return INPUT_EOF;
    free(input->text);
    input->text = 0;
    char *prompt = prompt_render(prompt_level);
    fflush(stdout);
    char *line = lineedit_read(prompt);
    free(prompt);
    prompt_level = 2;
    if (!line) {
        input->eof = 1;
        input->length = input->position = 0;
        return INPUT_EOF;
    }
    if (shell.interactive && (strchr(line, '!') || line[0] == '^')) {
        struct arena_mark mark = arena_mark(stack);
        int changed;
        char *expanded = history_expand(line, &changed);
        if (!expanded || changed) {
            char *next = xstrdup(expanded ? expanded : "");
            if (expanded) fprintf(stderr, "%s\n", expanded);
            free(line);
            line = next;
        }
        arena_release(stack, mark);
    }
    size_t length = strlen(line);
    if (length) history_add(line);
    input->text = xrealloc(line, length + 2);
    input->text[length] = '\n';
    input->text[length + 1] = 0;
    input->length = length + 1;
    input->position = 1;
    if (option_enabled(OPTION_VERBOSE)) fputs(input->text, stderr);
    return (unsigned char)input->text[0];
}

static int raw_getc(void) {
    for (;;) {
        struct input *input = current;
        if (!input) return INPUT_EOF;
        if (input->pushback_count) return input->pushback[--input->pushback_count];
        int c;
        switch (input->kind) {
        case INPUT_FILE: c = read_file(input); break;
        case INPUT_INTERACTIVE: c = read_interactive(input); break;
        default: c = input->position < input->length ?
                     (unsigned char)input->text[input->position++] : INPUT_EOF;
        }
        if (c == INPUT_EOF && input->kind == INPUT_ALIAS) {
            input_pop();
            continue;
        }
        return c;
    }
}

int input_getc(void) {
    int c = raw_getc();
    if (c == '\n' && current) current->line++;
    if (c != INPUT_EOF)
        for (int i = 0; i < capture_count; i++) strbuf_putc(captures[i], (char)c);
    return c;
}

void input_ungetc(int c) {
    if (c == INPUT_EOF || !current) return;
    if (current->pushback_count < PUSHBACK) current->pushback[current->pushback_count++] = c;
    if (c == '\n') current->line--;
    for (int i = 0; i < capture_count; i++)
        if (captures[i]->length) strbuf_truncate(captures[i], captures[i]->length - 1);
}

int input_peek(void) {
    int c = input_getc();
    input_ungetc(c);
    return c;
}

void input_discard_line(void) {
    if (!current) return;
    current->pushback_count = 0;
    while (current && current->kind == INPUT_ALIAS) input_pop();
    if (current && current->kind == INPUT_INTERACTIVE) {
        current->position = current->length;
        current->eof = 0;
    }
    prompt_level = 1;
}

int input_line(void) {
    return current ? current->line : shell.line;
}

void input_set_line(int line) {
    if (current) current->line = line;
}

const char *input_name(void) {
    struct input *input = current;
    while (input && input->kind == INPUT_ALIAS) input = input->previous;
    return input && input->name ? input->name : shell.script;
}

int input_interactive(void) {
    struct input *input = current;
    while (input && input->kind == INPUT_ALIAS) input = input->previous;
    return input && input->kind == INPUT_INTERACTIVE;
}

int input_alias_active(const struct alias *alias) {
    for (struct input *input = current; input && input->kind == INPUT_ALIAS;
         input = input->previous)
        if (input->alias == alias) return 1;
    return 0;
}

int input_at_alias_end_with_space(void) {
    struct input *input = current;
    if (!input || input->kind != INPUT_ALIAS || input->position < input->length) return 0;
    char last = input->length ? input->text[input->length - 1] : 0;
    return last == ' ' || last == '\t';
}

void input_prompt(int level) {
    prompt_level = level;
}

void input_capture_begin(struct strbuf *buffer) {
    if (capture_count < CAPTURE_DEPTH) captures[capture_count++] = buffer;
}

void input_capture_end(void) {
    if (capture_count) capture_count--;
}

void input_sync(void) {
    for (struct input *input = current; input; input = input->previous) {
        if (input->kind != INPUT_FILE || input->fd != 0 || !input->buffered) continue;
        size_t unread = input->buffer_length - input->buffer_position;
        if (unread && lseek(input->fd, -(off_t)unread, SEEK_CUR) >= 0)
            input->buffer_length = input->buffer_position;
    }
}
