#include <stdbool.h>
#include <stdint.h>

#include "initramfs.h"
#include "mem.h"

#define TAR_BLOCK 512
#define TAR_NAME_LEN 100

struct tar_header {
    char name[TAR_NAME_LEN];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
};

static const uint8_t *tar_base = NULL;
static uint64_t tar_size = 0;

static uint64_t parse_octal(const char *s, int n) {
    uint64_t value = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        value = value * 8 + (s[i] - '0');
    return value;
}

static bool name_matches(const char *field, const char *name) {
    int i = 0;
    for (; i < TAR_NAME_LEN && name[i] != '\0'; i++)
        if (field[i] != name[i])
            return false;
    return i < TAR_NAME_LEN && field[i] == '\0';
}

void initramfs_init(const uint8_t *base, uint64_t size) {
    tar_base = base;
    tar_size = size;
}

bool initramfs_find(const char *name, const uint8_t **data, uint64_t *size) {
    uint64_t off = 0;

    while (tar_size >= TAR_BLOCK && off <= tar_size - TAR_BLOCK) {
        const struct tar_header *h = (const struct tar_header *)(tar_base + off);
        if (memcmp(h->magic, "ustar", 5) != 0)
            return false;

        uint64_t file_size = parse_octal(h->size, sizeof(h->size));
        if (file_size > tar_size - off - TAR_BLOCK)
            return false;

        if ((h->typeflag == '0' || h->typeflag == '\0') && name_matches(h->name, name)) {
            *data = tar_base + off + TAR_BLOCK;
            *size = file_size;
            return true;
        }

        off += TAR_BLOCK + ((file_size + TAR_BLOCK - 1) & ~(uint64_t)(TAR_BLOCK - 1));
    }
    return false;
}
