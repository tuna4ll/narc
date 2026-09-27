#include "internal.h"
#include "../internal/narc.h"

struct dirent *readdir(DIR *directory) {
    narc_dir_entry_t source;
    long result = __libc_result(narc_read_dir(directory->descriptor, &source));
    if (result <= 0) return 0;

    directory->entry.d_ino = source.inode;
    directory->entry.d_type = source.type == NARC_FILE_DIRECTORY ? DT_DIR :
                              source.type == NARC_FILE_REGULAR ? DT_REG : DT_UNKNOWN;
    for (size_t i = 0; i < sizeof(directory->entry.d_name); i++)
        directory->entry.d_name[i] = source.name[i];
    return &directory->entry;
}
