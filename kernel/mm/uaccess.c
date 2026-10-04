#include <kernel/string.h>
#include <kernel/uaccess.h>

static void *user_chunk(struct address_space *space, uint64_t address, size_t len,
                        size_t *chunk) {
    uint64_t phys = vmm_user_phys(space, address);
    if (!phys) return 0;
    *chunk = PAGE_SIZE - (size_t)(address & (PAGE_SIZE - 1));
    if (*chunk > len) *chunk = len;
    return phys_to_virt(phys);
}

int uaccess_read(struct address_space *space, void *dst, uint64_t src, size_t len) {
    uint8_t *out = dst;
    while (len) {
        size_t chunk;
        const void *in = user_chunk(space, src, len, &chunk);
        if (!in) return -1;
        memcpy(out, in, chunk);
        src += chunk;
        out += chunk;
        len -= chunk;
    }
    return 0;
}

int uaccess_write(struct address_space *space, uint64_t dst, const void *src, size_t len) {
    const uint8_t *in = src;
    while (len) {
        size_t chunk;
        void *out = user_chunk(space, dst, len, &chunk);
        if (!out) return -1;
        memcpy(out, in, chunk);
        dst += chunk;
        in += chunk;
        len -= chunk;
    }
    return 0;
}

int uaccess_zero(struct address_space *space, uint64_t dst, size_t len) {
    while (len) {
        size_t chunk;
        void *out = user_chunk(space, dst, len, &chunk);
        if (!out) return -1;
        memset(out, 0, chunk);
        dst += chunk;
        len -= chunk;
    }
    return 0;
}

long uaccess_strlen(struct address_space *space, uint64_t src) {
    for (long length = 0;;) {
        size_t chunk;
        const char *in = user_chunk(space, src, PAGE_SIZE, &chunk);
        if (!in) return -1;
        for (size_t i = 0; i < chunk; i++)
            if (!in[i]) return length + (long)i;
        length += (long)chunk;
        src += chunk;
    }
}
