#pragma once

#include <caposh/ast.h>

struct saved_fd;

struct redirection {
    struct saved_fd *saved;
};

int redirect_apply(struct redirect *list, struct redirection *state, int save);
void redirect_restore(struct redirection *state);
void redirect_commit(struct redirection *state);
int redirect_move_high(int fd);
