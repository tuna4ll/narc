#include <caposh/alias.h>
#include <caposh/builtins.h>
#include <caposh/exec.h>
#include <caposh/history.h>
#include <caposh/lineedit.h>
#include <caposh/memory.h>
#include <caposh/prompt.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define CTRL(c) ((c) & 0x1f)
#define KEY_ESCAPE 0x1b
#define KEY_DELETE 0x7f

enum key {
    KEY_NONE = 256,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_DEL,
    KEY_WORD_LEFT, KEY_WORD_RIGHT, KEY_WORD_DELETE, KEY_WORD_RUBOUT,
    KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_UPCASE_WORD, KEY_DOWNCASE_WORD, KEY_CAPITAL_WORD,
    KEY_YANK_LAST,
};

struct editor {
    int fd;
    char *buffer;
    size_t length, capacity, cursor;
    const char *prompt;
    size_t prompt_width;
    size_t columns;
    size_t rows_used, cursor_row;
    size_t history_index;
    char *saved_line;
    char *kill_ring;
    int last_tab;
};

static struct termios original;
static int raw_active;
static int raw_fd = -1;

void lineedit_restore_terminal(void) {
    if (!raw_active) return;
    tcsetattr(raw_fd, TCSADRAIN, &original);
    raw_active = 0;
}

static int enable_raw(int fd) {
    if (tcgetattr(fd, &original) != 0) return -1;
    struct termios raw = original;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSADRAIN, &raw) != 0) return -1;
    raw_active = 1;
    raw_fd = fd;
    return 0;
}

static void out(struct editor *e, const char *data, size_t length) {
    while (length) {
        ssize_t wrote = write(e->fd == 0 ? 2 : e->fd, data, length);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return;
        data += wrote;
        length -= (size_t)wrote;
    }
}

static void outs(struct editor *e, const char *text) {
    out(e, text, strlen(text));
}

static size_t terminal_columns(int fd) {
    struct winsize size;
    if (ioctl(fd, TIOCGWINSZ, &size) == 0 && size.ws_col) return size.ws_col;
    const char *columns = getenv("COLUMNS");
    return columns && atoi(columns) > 0 ? (size_t)atoi(columns) : 80;
}

static int read_byte(int fd) {
    unsigned char c;
    for (;;) {
        ssize_t got = read(fd, &c, 1);
        if (got == 1) return c;
        if (got < 0 && errno == EINTR) continue;
        return -1;
    }
}

static int read_key(int fd) {
    int c = read_byte(fd);
    if (c != KEY_ESCAPE) return c;
    int next = read_byte(fd);
    if (next < 0) return KEY_ESCAPE;
    if (next == 'b' || next == 'B') return KEY_WORD_LEFT;
    if (next == 'f' || next == 'F') return KEY_WORD_RIGHT;
    if (next == 'd' || next == 'D') return KEY_WORD_DELETE;
    if (next == 'u' || next == 'U') return KEY_UPCASE_WORD;
    if (next == 'l' || next == 'L') return KEY_DOWNCASE_WORD;
    if (next == 'c' || next == 'C') return KEY_CAPITAL_WORD;
    if (next == '.' || next == '_') return KEY_YANK_LAST;
    if (next == KEY_DELETE || next == CTRL('H')) return KEY_WORD_RUBOUT;
    if (next == 'O') {
        int final = read_byte(fd);
        switch (final) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        default: return KEY_NONE;
        }
    }
    if (next != '[') return KEY_NONE;
    char parameters[16];
    size_t count = 0;
    int final;
    for (;;) {
        final = read_byte(fd);
        if (final < 0) return KEY_NONE;
        if (final >= 0x40 && final <= 0x7e) break;
        if (count < sizeof(parameters) - 1) parameters[count++] = (char)final;
    }
    parameters[count] = 0;
    int modified = strstr(parameters, ";5") != 0 || strstr(parameters, ";3") != 0;
    switch (final) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return modified ? KEY_WORD_RIGHT : KEY_RIGHT;
    case 'D': return modified ? KEY_WORD_LEFT : KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case '~':
        switch (atoi(parameters)) {
        case 1: case 7: return KEY_HOME;
        case 4: case 8: return KEY_END;
        case 3: return KEY_DEL;
        case 5: return KEY_PAGE_UP;
        case 6: return KEY_PAGE_DOWN;
        default: return KEY_NONE;
        }
    default:
        return KEY_NONE;
    }
}

static void ensure(struct editor *e, size_t extra) {
    if (e->length + extra + 1 <= e->capacity) return;
    while (e->length + extra + 1 > e->capacity) e->capacity = e->capacity ? e->capacity * 2 : 128;
    e->buffer = xrealloc(e->buffer, e->capacity);
}

static void set_line(struct editor *e, const char *text) {
    size_t length = strlen(text);
    e->length = 0;
    ensure(e, length);
    memcpy(e->buffer, text, length + 1);
    e->length = e->cursor = length;
}

static size_t display_width(const char *text, size_t length) {
    size_t width = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if ((c & 0xc0) == 0x80) continue;
        width += c < 32 || c == 0x7f ? 2 : 1;
    }
    return width;
}

static void put_visible(struct strbuf *line, const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 || c == 0x7f) {
            strbuf_putc(line, '^');
            strbuf_putc(line, c == 0x7f ? '?' : (char)(c + '@'));
        } else {
            strbuf_putc(line, (char)c);
        }
    }
}

static void put_prompt(struct strbuf *line, const char *prompt) {
    for (const char *s = prompt; *s; s++) {
        if (*s == '\001' || *s == '\002') continue;
        if (*s == '\n') strbuf_putc(line, '\r');
        strbuf_putc(line, *s);
    }
}

static void refresh(struct editor *e) {
    struct strbuf line;
    strbuf_init(&line, stack);
    size_t columns = e->columns ? e->columns : 80;
    size_t total = e->prompt_width + display_width(e->buffer, e->length);
    size_t position = e->prompt_width + display_width(e->buffer, e->cursor);
    size_t rows = total / columns + 1;
    size_t cursor_row = position / columns;
    size_t cursor_col = position % columns;
    if (e->cursor_row) strbuf_printf(&line, "\x1b[%zuA", e->cursor_row);
    strbuf_putc(&line, '\r');
    const char *last_line = strrchr(e->prompt, '\n');
    put_prompt(&line, last_line ? last_line + 1 : e->prompt);
    put_visible(&line, e->buffer, e->length);
    if (total && total % columns == 0) strbuf_append(&line, "\n\r", 2);
    strbuf_append(&line, "\x1b[J", 3);
    size_t end_row = total / columns;
    if (end_row > cursor_row) strbuf_printf(&line, "\x1b[%zuA", end_row - cursor_row);
    strbuf_putc(&line, '\r');
    if (cursor_col) strbuf_printf(&line, "\x1b[%zuC", cursor_col);
    out(e, line.data, line.length);
    e->rows_used = rows;
    e->cursor_row = cursor_row;
}

static void insert(struct editor *e, const char *text, size_t length) {
    ensure(e, length);
    memmove(e->buffer + e->cursor + length, e->buffer + e->cursor, e->length - e->cursor + 1);
    memcpy(e->buffer + e->cursor, text, length);
    e->length += length;
    e->cursor += length;
}

static void erase(struct editor *e, size_t start, size_t end, int save) {
    if (end <= start) return;
    if (save) {
        free(e->kill_ring);
        e->kill_ring = xstrndup(e->buffer + start, end - start);
    }
    memmove(e->buffer + start, e->buffer + end, e->length - end + 1);
    e->length -= end - start;
    if (e->cursor > end) e->cursor -= end - start;
    else if (e->cursor > start) e->cursor = start;
}

static size_t previous_char(const struct editor *e, size_t at) {
    if (!at) return 0;
    at--;
    while (at && ((unsigned char)e->buffer[at] & 0xc0) == 0x80) at--;
    return at;
}

static size_t next_char(const struct editor *e, size_t at) {
    if (at >= e->length) return e->length;
    at++;
    while (at < e->length && ((unsigned char)e->buffer[at] & 0xc0) == 0x80) at++;
    return at;
}

static int word_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || ((unsigned char)c & 0x80);
}

static size_t word_left(const struct editor *e) {
    size_t at = e->cursor;
    while (at && !word_char(e->buffer[at - 1])) at--;
    while (at && word_char(e->buffer[at - 1])) at--;
    return at;
}

static size_t word_right(const struct editor *e) {
    size_t at = e->cursor;
    while (at < e->length && !word_char(e->buffer[at])) at++;
    while (at < e->length && word_char(e->buffer[at])) at++;
    return at;
}

static void history_move(struct editor *e, int direction) {
    size_t count = history_count();
    if (!count) return;
    if (e->history_index == count) {
        free(e->saved_line);
        e->saved_line = xstrdup(e->buffer);
    }
    if (direction < 0 && e->history_index > 0) e->history_index--;
    else if (direction > 0 && e->history_index < count) e->history_index++;
    else return;
    set_line(e, e->history_index == count ? e->saved_line : history_get(e->history_index));
}

struct candidates {
    char **items;
    size_t count, capacity;
};

static void candidate_add(struct candidates *list, const char *text) {
    for (size_t i = 0; i < list->count; i++)
        if (!strcmp(list->items[i], text)) return;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 32;
        char **items = stalloc(capacity * sizeof(char *));
        if (list->count) memcpy(items, list->items, list->count * sizeof(char *));
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = ststrdup(text);
}

static int compare(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void complete_commands(struct candidates *list, const char *prefix) {
    size_t length = strlen(prefix);
    int count;
    const struct builtin *builtins = builtin_table(&count);
    for (int i = 0; i < count; i++)
        if (!strncmp(builtins[i].name, prefix, length)) candidate_add(list, builtins[i].name);
    struct function **functions = functions_sorted(&count);
    for (int i = 0; i < count; i++)
        if (!strncmp(functions[i]->name, prefix, length)) candidate_add(list, functions[i]->name);
    static const char *const keywords[] = {
        "if", "then", "else", "elif", "fi", "case", "esac", "for", "while", "until", "do",
        "done", "function", "in", 0,
    };
    for (int i = 0; keywords[i]; i++)
        if (!strncmp(keywords[i], prefix, length)) candidate_add(list, keywords[i]);
    const char *path = var_get("PATH");
    char *copy = ststrdup(path ? path : "");
    char *state;
    for (char *dir = strtok_r(copy, ":", &state); dir; dir = strtok_r(0, ":", &state)) {
        DIR *handle = opendir(dir);
        if (!handle) continue;
        struct dirent *entry;
        while ((entry = readdir(handle))) {
            if (entry->d_name[0] == '.' || strncmp(entry->d_name, prefix, length)) continue;
            struct strbuf full;
            strbuf_init(&full, stack);
            strbuf_printf(&full, "%s/%s", dir, entry->d_name);
            struct stat info;
            if (stat(strbuf_string(&full), &info) == 0 && S_ISREG(info.st_mode) &&
                (info.st_mode & 0111))
                candidate_add(list, entry->d_name);
        }
        closedir(handle);
    }
}

static void complete_variables(struct candidates *list, const char *prefix) {
    size_t count, length = strlen(prefix);
    struct var **vars = vars_sorted(&count);
    for (size_t i = 0; i < count; i++) {
        if (strncmp(vars[i]->name, prefix, length)) continue;
        struct strbuf item;
        strbuf_init(&item, stack);
        strbuf_printf(&item, "$%s", vars[i]->name);
        candidate_add(list, strbuf_string(&item));
    }
}

static void complete_files(struct candidates *list, const char *word, int only_executable) {
    const char *slash = strrchr(word, '/');
    char *directory = slash ? ststrndup(word, (size_t)(slash - word + 1)) : ststrdup("");
    const char *base = slash ? slash + 1 : word;
    char *open_path = *directory ? directory : ".";
    if (directory[0] == '~') {
        const char *home = var_get("HOME");
        if (home && (directory[1] == '/' || !directory[1])) {
            struct strbuf expanded;
            strbuf_init(&expanded, stack);
            strbuf_puts(&expanded, home);
            strbuf_puts(&expanded, directory + 1);
            open_path = strbuf_string(&expanded);
        }
    }
    DIR *handle = opendir(open_path);
    if (!handle) return;
    size_t length = strlen(base);
    struct dirent *entry;
    while ((entry = readdir(handle))) {
        const char *name = entry->d_name;
        if (strncmp(name, base, length)) continue;
        if (name[0] == '.' && base[0] != '.') continue;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        struct strbuf full, item;
        strbuf_init(&full, stack);
        strbuf_init(&item, stack);
        strbuf_puts(&full, open_path);
        if (*open_path && open_path[strlen(open_path) - 1] != '/') strbuf_putc(&full, '/');
        strbuf_puts(&full, name);
        struct stat info;
        int dir = stat(strbuf_string(&full), &info) == 0 && S_ISDIR(info.st_mode);
        if (only_executable && !dir && !(info.st_mode & 0111)) continue;
        strbuf_puts(&item, directory);
        strbuf_puts(&item, name);
        if (dir) strbuf_putc(&item, '/');
        candidate_add(list, strbuf_string(&item));
    }
    closedir(handle);
}

static int command_position(const struct editor *e, size_t start) {
    size_t at = start;
    while (at && (e->buffer[at - 1] == ' ' || e->buffer[at - 1] == '\t')) at--;
    if (!at) return 1;
    char c = e->buffer[at - 1];
    return c == ';' || c == '|' || c == '&' || c == '(' || c == '`' ||
           (c == '(' && at > 1 && e->buffer[at - 2] == '$');
}

static char *escape_candidate(const char *text) {
    struct strbuf out;
    strbuf_init(&out, stack);
    for (const char *s = text; *s; s++) {
        if (strchr(" \t\\'\"$`&|;<>()*?[]!#", *s) && !(s == text && *s == '$')) strbuf_putc(&out, '\\');
        strbuf_putc(&out, *s);
    }
    return strbuf_string(&out);
}

static void list_candidates(struct editor *e, struct candidates *list) {
    size_t width = 0;
    for (size_t i = 0; i < list->count; i++) {
        size_t length = strlen(list->items[i]);
        if (length > width) width = length;
    }
    width += 2;
    size_t per_row = e->columns / width ? e->columns / width : 1;
    size_t rows = (list->count + per_row - 1) / per_row;
    struct strbuf text;
    strbuf_init(&text, stack);
    strbuf_append(&text, "\r\n", 2);
    for (size_t row = 0; row < rows; row++) {
        for (size_t column = 0; column < per_row; column++) {
            size_t index = column * rows + row;
            if (index >= list->count) break;
            const char *item = list->items[index];
            const char *slash = strrchr(item, '/');
            const char *shown = slash && slash[1] ? slash + 1 : item;
            if (slash && !slash[1]) {
                const char *previous = item + strlen(item) - 1;
                while (previous > item && previous[-1] != '/') previous--;
                shown = previous;
            }
            strbuf_puts(&text, shown);
            if (column + 1 < per_row && index + rows < list->count)
                for (size_t pad = strlen(shown); pad < width; pad++) strbuf_putc(&text, ' ');
        }
        strbuf_append(&text, "\r\n", 2);
    }
    out(e, text.data, text.length);
    e->cursor_row = 0;
}

static void complete(struct editor *e) {
    struct arena_mark mark = arena_mark(stack);
    size_t start = e->cursor;
    while (start && !strchr(" \t;|&<>()`", e->buffer[start - 1])) start--;
    char *word = ststrndup(e->buffer + start, e->cursor - start);
    char *plain = ststrdup(word);
    size_t w = 0;
    for (size_t r = 0; word[r]; r++) {
        if (word[r] == '\\' && word[r + 1]) r++;
        plain[w++] = word[r];
    }
    plain[w] = 0;
    struct candidates list = { 0, 0, 0 };
    if (plain[0] == '$' && !strchr(plain, '/')) complete_variables(&list, plain + 1);
    else if (command_position(e, start) && !strchr(plain, '/')) complete_commands(&list, plain);
    else complete_files(&list, plain, 0);
    if (!list.count) {
        outs(e, "\a");
        arena_release(stack, mark);
        return;
    }
    qsort(list.items, list.count, sizeof(char *), compare);
    size_t common = strlen(list.items[0]);
    for (size_t i = 1; i < list.count; i++) {
        size_t j = 0;
        while (j < common && list.items[i][j] == list.items[0][j]) j++;
        common = j;
    }
    char *prefix = ststrndup(list.items[0], common);
    if (common > strlen(plain) || list.count == 1) {
        char *replacement = escape_candidate(prefix);
        erase(e, start, e->cursor, 0);
        insert(e, replacement, strlen(replacement));
        if (list.count == 1 && prefix[common - 1] != '/') insert(e, " ", 1);
        e->last_tab = 0;
    } else if (e->last_tab) {
        list_candidates(e, &list);
        e->last_tab = 0;
    } else {
        outs(e, "\a");
        e->last_tab = 1;
        arena_release(stack, mark);
        return;
    }
    arena_release(stack, mark);
}

static int reverse_search(struct editor *e) {
    char query[128] = "";
    size_t query_length = 0;
    size_t match = history_count();
    for (;;) {
        struct strbuf line;
        strbuf_init(&line, stack);
        const char *found = match < history_count() ? history_get(match) : "";
        if (e->cursor_row) strbuf_printf(&line, "\x1b[%zuA", e->cursor_row);
        strbuf_printf(&line, "\r(reverse-i-search)`%s': %s\x1b[J", query, found);
        out(e, line.data, line.length);
        e->cursor_row = (19 + query_length + strlen(found)) / (e->columns ? e->columns : 80);
        int key = read_key(e->fd);
        if (key == CTRL('R')) {
            for (size_t i = match; i-- > 0;) {
                if (strstr(history_get(i), query)) {
                    match = i;
                    break;
                }
            }
            continue;
        }
        if (key == KEY_DELETE || key == CTRL('H')) {
            if (query_length) query[--query_length] = 0;
            continue;
        }
        if (key == CTRL('G') || key == CTRL('C')) {
            break;
        }
        if (key >= 32 && key < 127 && query_length < sizeof(query) - 1) {
            query[query_length++] = (char)key;
            query[query_length] = 0;
            for (size_t i = match < history_count() && strstr(history_get(match), query) ? match + 1 :
                            history_count();
                 i-- > 0;) {
                if (strstr(history_get(i), query)) {
                    match = i;
                    break;
                }
            }
            continue;
        }
        if (match < history_count()) set_line(e, history_get(match));
        if (key == '\r' || key == '\n') return 1;
        break;
    }
    refresh(e);
    return 0;
}

static void transform_word(struct editor *e, int mode) {
    size_t at = e->cursor;
    while (at < e->length && !word_char(e->buffer[at])) at++;
    int first = 1;
    while (at < e->length && word_char(e->buffer[at])) {
        char c = e->buffer[at];
        if (mode == 0) c = (char)toupper((unsigned char)c);
        else if (mode == 1) c = (char)tolower((unsigned char)c);
        else c = (char)(first ? toupper((unsigned char)c) : tolower((unsigned char)c));
        e->buffer[at++] = c;
        first = 0;
    }
    e->cursor = at;
}

static char *read_plain(void) {
    struct strbuf line;
    strbuf_init(&line, 0);
    for (;;) {
        int c = read_byte(0);
        if (c < 0) {
            if (!line.length) {
                strbuf_free(&line);
                return 0;
            }
            break;
        }
        if (c == '\n') break;
        strbuf_putc(&line, (char)c);
    }
    return strbuf_string(&line);
}

char *lineedit_read(const char *prompt) {
    int fd = 0;
    const char *term = getenv("TERM");
    if (!isatty(fd) || !isatty(2) || (term && !strcmp(term, "dumb"))) {
        if (isatty(fd) || shell.interactive) {
            fputs(prompt, stderr);
            fflush(stderr);
        }
        return read_plain();
    }
    struct editor e;
    memset(&e, 0, sizeof(e));
    e.fd = fd;
    e.prompt = prompt;
    const char *last_line = strrchr(prompt, '\n');
    e.prompt_width = prompt_visible_width(last_line ? last_line + 1 : prompt);
    e.columns = terminal_columns(fd);
    e.history_index = history_count();
    ensure(&e, 0);
    e.buffer[0] = 0;
    if (enable_raw(fd) != 0) return read_plain();
    if (last_line) {
        struct strbuf head;
        strbuf_init(&head, stack);
        put_prompt(&head, ststrndup(prompt, (size_t)(last_line - prompt + 1)));
        out(&e, head.data, head.length);
    }
    refresh(&e);
    char *result = 0;
    for (;;) {
        int key = read_key(fd);
        if (key != '\t') e.last_tab = 0;
        if (key < 0) {
            if (!e.length) break;
            key = '\r';
        }
        if (key == CTRL('R')) key = reverse_search(&e) ? '\r' : KEY_NONE;
        switch (key) {
        case '\r':
        case '\n':
            e.cursor = e.length;
            refresh(&e);
            outs(&e, "\r\n");
            result = xstrdup(e.buffer);
            goto done;
        case CTRL('C'):
            e.cursor = e.length;
            refresh(&e);
            outs(&e, "^C\r\n");
            shell.status = 130;
            set_line(&e, "");
            e.cursor_row = 0;
            refresh(&e);
            break;
        case CTRL('D'):
            if (!e.length) {
                outs(&e, "\r\n");
                goto done;
            }
            erase(&e, e.cursor, next_char(&e, e.cursor), 0);
            break;
        case KEY_DELETE:
        case CTRL('H'):
            erase(&e, previous_char(&e, e.cursor), e.cursor, 0);
            break;
        case KEY_DEL:
            erase(&e, e.cursor, next_char(&e, e.cursor), 0);
            break;
        case KEY_LEFT:
        case CTRL('B'):
            e.cursor = previous_char(&e, e.cursor);
            break;
        case KEY_RIGHT:
        case CTRL('F'):
            e.cursor = next_char(&e, e.cursor);
            break;
        case KEY_HOME:
        case CTRL('A'):
            e.cursor = 0;
            break;
        case KEY_END:
        case CTRL('E'):
            e.cursor = e.length;
            break;
        case KEY_WORD_LEFT:
            e.cursor = word_left(&e);
            break;
        case KEY_WORD_RIGHT:
            e.cursor = word_right(&e);
            break;
        case KEY_UP:
        case CTRL('P'):
            history_move(&e, -1);
            break;
        case KEY_DOWN:
        case CTRL('N'):
            history_move(&e, 1);
            break;
        case KEY_PAGE_UP:
            e.history_index = 1;
            history_move(&e, -1);
            break;
        case KEY_PAGE_DOWN:
            e.history_index = history_count() - 1;
            history_move(&e, 1);
            break;
        case CTRL('K'):
            erase(&e, e.cursor, e.length, 1);
            break;
        case CTRL('U'):
            erase(&e, 0, e.cursor, 1);
            break;
        case CTRL('W'): {
            size_t at = e.cursor;
            while (at && isspace((unsigned char)e.buffer[at - 1])) at--;
            while (at && !isspace((unsigned char)e.buffer[at - 1])) at--;
            erase(&e, at, e.cursor, 1);
            break;
        }
        case KEY_WORD_RUBOUT:
            erase(&e, word_left(&e), e.cursor, 1);
            break;
        case KEY_WORD_DELETE:
            erase(&e, e.cursor, word_right(&e), 1);
            break;
        case CTRL('Y'):
            if (e.kill_ring) insert(&e, e.kill_ring, strlen(e.kill_ring));
            break;
        case CTRL('T'):
            if (e.cursor && e.length > 1) {
                if (e.cursor == e.length) e.cursor--;
                char c = e.buffer[e.cursor - 1];
                e.buffer[e.cursor - 1] = e.buffer[e.cursor];
                e.buffer[e.cursor] = c;
                e.cursor++;
            }
            break;
        case CTRL('L'):
            outs(&e, "\x1b[H\x1b[2J");
            e.cursor_row = 0;
            break;
        case KEY_UPCASE_WORD:
            transform_word(&e, 0);
            break;
        case KEY_DOWNCASE_WORD:
            transform_word(&e, 1);
            break;
        case KEY_CAPITAL_WORD:
            transform_word(&e, 2);
            break;
        case KEY_YANK_LAST:
            if (history_count()) {
                const char *last = history_get(history_count() - 1);
                const char *end = last + strlen(last);
                while (end > last && isspace((unsigned char)end[-1])) end--;
                const char *start = end;
                while (start > last && !isspace((unsigned char)start[-1])) start--;
                insert(&e, start, (size_t)(end - start));
            }
            break;
        case '\t':
            complete(&e);
            break;
        case CTRL('V'): {
            int literal = read_byte(fd);
            if (literal >= 0) {
                char c = (char)literal;
                insert(&e, &c, 1);
            }
            break;
        }
        default:
            if (key >= 32 && key < 256 && key != KEY_DELETE) {
                char c = (char)key;
                insert(&e, &c, 1);
            }
            break;
        }
        refresh(&e);
    }
done:
    lineedit_restore_terminal();
    free(e.buffer);
    free(e.saved_line);
    free(e.kill_ring);
    return result;
}
