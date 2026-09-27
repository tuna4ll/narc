#pragma once

#include <dirent.h>

struct __libc_dir {
    int descriptor;
    struct dirent entry;
};
