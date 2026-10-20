#pragma once

#include <sys/types.h>

#define ST_RDONLY 1ul
#define ST_NOSUID 2ul

typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    fsblkcnt_t f_blocks;
    fsblkcnt_t f_bfree;
    fsblkcnt_t f_bavail;
    fsfilcnt_t f_files;
    fsfilcnt_t f_ffree;
    fsfilcnt_t f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
    char f_basetype[16];
};

int statvfs(const char *restrict path, struct statvfs *restrict buffer);
int fstatvfs(int fd, struct statvfs *buffer);
