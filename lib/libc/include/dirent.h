#pragma once

#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10

typedef struct __libc_dir DIR;

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[256];
};

DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
int dirfd(DIR *directory);
