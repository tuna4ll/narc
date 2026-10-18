#include "internal.h"
#include "../internal/narc.h"

struct dirent *readdir(DIR *directory) {
    narc_dir_entry_t source;
    long result = __libc_result(narc_read_dir(directory->descriptor, &source));
    if (result <= 0) return 0;

    directory->entry.d_ino = source.inode;
    switch (source.type) {
    case NARC_FILE_DIRECTORY: directory->entry.d_type = DT_DIR; break;
    case NARC_FILE_REGULAR: directory->entry.d_type = DT_REG; break;
    case NARC_FILE_CHARACTER: directory->entry.d_type = DT_CHR; break;
    case NARC_FILE_BLOCK: directory->entry.d_type = DT_BLK; break;
    case NARC_FILE_SYMLINK: directory->entry.d_type = DT_LNK; break;
    default: directory->entry.d_type = DT_UNKNOWN; break;
    }
    for (size_t i = 0; i < sizeof(directory->entry.d_name); i++)
        directory->entry.d_name[i] = source.name[i];
    return &directory->entry;
}
