#pragma once

#include <narc/narc.h>

int __set_attr_path(int dirfd, const char *path, const narc_file_attr_t *attr, int nofollow);
int __set_attr_fd(int fd, const narc_file_attr_t *attr);
