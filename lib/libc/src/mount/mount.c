#include "../internal/narc.h"
#include <errno.h>
#include <sys/mount.h>

int mount(const char *source, const char *target, const char *type, unsigned long flags,
          const void *data) {
    (void)data;
    if (flags & ~MS_RDONLY) {
        errno = EINVAL;
        return -1;
    }
    uint32_t native = (flags & MS_RDONLY) ? NARC_MOUNT_READONLY : 0;
    return (int)__libc_result(narc_mount(source, target, type, native));
}

int umount(const char *target) {
    return (int)__libc_result(narc_umount(target));
}
