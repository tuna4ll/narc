#include <caposh/error.h>
#include <caposh/expand.h>
#include <caposh/jobs.h>
#include <caposh/options.h>
#include <caposh/redirect.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PIPE_CAPACITY 4096

struct saved_fd {
    struct saved_fd *next;
    int fd;
    int copy;
};

int redirect_move_high(int fd) {
    if (fd < 0 || fd >= 10) return fd;
    int moved = fcntl(fd, F_DUPFD_CLOEXEC, 10);
    if (moved < 0) return fd;
    close(fd);
    return moved;
}

static int heredoc_fd(const char *body) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return -1;
    size_t length = strlen(body);
    if (length <= PIPE_CAPACITY) {
        size_t done = 0;
        while (done < length) {
            ssize_t wrote = write(fds[1], body + done, length - done);
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) break;
            done += (size_t)wrote;
        }
        close(fds[1]);
        return fds[0];
    }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        size_t done = 0;
        while (done < length) {
            ssize_t wrote = write(fds[1], body + done, length - done);
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) break;
            done += (size_t)wrote;
        }
        _exit(0);
    }
    close(fds[1]);
    return fds[0];
}

static int save(struct redirection *state, int fd) {
    if (!state) return 0;
    for (struct saved_fd *saved = state->saved; saved; saved = saved->next)
        if (saved->fd == fd) return 0;
    struct saved_fd *saved = xmalloc(sizeof(*saved));
    saved->fd = fd;
    saved->copy = fcntl(fd, F_DUPFD_CLOEXEC, 10);
    if (saved->copy < 0 && errno != EBADF) {
        free(saved);
        return -1;
    }
    saved->next = state->saved;
    state->saved = saved;
    return 0;
}

static int all_digits(const char *text) {
    if (!*text) return 0;
    for (; *text; text++) if (!isdigit((unsigned char)*text)) return 0;
    return 1;
}

static int open_target(struct redirect *r, const char *path) {
    int flags;
    switch (r->type) {
    case REDIRECT_INPUT: flags = O_RDONLY; break;
    case REDIRECT_READ_WRITE: flags = O_RDWR | O_CREAT; break;
    case REDIRECT_APPEND:
    case REDIRECT_APPEND_ERROR: flags = O_WRONLY | O_CREAT | O_APPEND; break;
    case REDIRECT_OUTPUT:
    case REDIRECT_OUTPUT_ERROR:
        if (option_enabled(OPTION_NOCLOBBER)) {
            struct stat info;
            if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
                fprintf(stderr, "%s: %s: cannot overwrite existing file\n", shell.name, path);
                errno = EEXIST;
                return -2;
            }
        }
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    default: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
    }
    return open(path, flags | O_CLOEXEC, 0666);
}

static int apply(struct redirect *r, struct redirection *state) {
    int source = -1, close_target = 0;
    const char *label = r->target->text;
    switch (r->type) {
    case REDIRECT_HEREDOC:
        source = heredoc_fd(r->heredoc_quoted ? r->heredoc : expand_heredoc(r->heredoc));
        label = "here-document";
        break;
    case REDIRECT_HERESTRING: {
        struct strbuf text;
        strbuf_init(&text, stack);
        strbuf_puts(&text, expand_string(r->target->text, EXPAND_TILDE));
        strbuf_putc(&text, '\n');
        source = heredoc_fd(strbuf_string(&text));
        label = "here-string";
        break;
    }
    case REDIRECT_DUP_INPUT:
    case REDIRECT_DUP_OUTPUT: {
        char *word = expand_single(r->target, r->target->text);
        if (!strcmp(word, "-")) {
            close_target = 1;
            break;
        }
        if (all_digits(word)) {
            int fd = atoi(word);
            if (fd == r->fd) return 0;
            if (fcntl(fd, F_GETFD) < 0) {
                fprintf(stderr, "%s: %s: bad file descriptor\n", shell.name, word);
                return -1;
            }
            if (save(state, r->fd) != 0 || dup2(fd, r->fd) < 0) {
                sh_perror(word);
                return -1;
            }
            return 0;
        }
        if (r->type == REDIRECT_DUP_OUTPUT && r->fd == 1) {
            struct redirect alternate = *r;
            alternate.type = REDIRECT_OUTPUT_ERROR;
            source = open_target(&alternate, word);
            if (source >= 0) {
                if (save(state, 1) != 0 || save(state, 2) != 0) return -1;
                dup2(source, 1);
                dup2(source, 2);
                close(source);
                return 0;
            }
            label = word;
            break;
        }
        fprintf(stderr, "%s: %s: ambiguous redirect\n", shell.name, word);
        return -1;
    }
    default: {
        char *path = expand_single(r->target, r->target->text);
        label = path;
        source = open_target(r, path);
        if (source == -2) return -1;
        break;
    }
    }
    if (close_target) {
        if (save(state, r->fd) != 0) return -1;
        close(r->fd);
        return 0;
    }
    if (source < 0) {
        sh_perror(label);
        return -1;
    }
    int both = r->type == REDIRECT_OUTPUT_ERROR || r->type == REDIRECT_APPEND_ERROR;
    if (save(state, both ? 1 : r->fd) != 0 || (both && save(state, 2) != 0)) {
        close(source);
        return -1;
    }
    int target = both ? 1 : r->fd;
    if (source != target) {
        if (dup2(source, target) < 0) {
            sh_perror(label);
            close(source);
            return -1;
        }
        close(source);
    } else {
        fcntl(target, F_SETFD, 0);
    }
    if (both) dup2(1, 2);
    return 0;
}

int redirect_apply(struct redirect *list, struct redirection *state, int save_state) {
    if (state) state->saved = 0;
    for (struct redirect *r = list; r; r = r->next) {
        shell.line = r->target->line;
        if (apply(r, save_state ? state : 0) != 0) return -1;
    }
    return 0;
}

void redirect_restore(struct redirection *state) {
    struct saved_fd *saved = state->saved;
    while (saved) {
        struct saved_fd *next = saved->next;
        if (saved->copy >= 0) {
            dup2(saved->copy, saved->fd);
            close(saved->copy);
        } else {
            close(saved->fd);
        }
        free(saved);
        saved = next;
    }
    state->saved = 0;
}

void redirect_commit(struct redirection *state) {
    struct saved_fd *saved = state->saved;
    while (saved) {
        struct saved_fd *next = saved->next;
        if (saved->copy >= 0) close(saved->copy);
        free(saved);
        saved = next;
    }
    state->saved = 0;
}
