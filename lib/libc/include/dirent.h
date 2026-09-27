#pragma once

#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_CHR     2
#define DT_DIR     4
#define DT_REG     8

typedef struct __libc_dir DIR;

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[64];
};

DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
int dirfd(DIR *directory);
