#include <kernel/string.h>
#include <kernel/system.h>
#include <kernel/uaccess.h>
#include <narc/abi.h>

#if defined(__x86_64__)
#define MACHINE "x86_64"
#elif defined(__aarch64__)
#define MACHINE "aarch64"
#else
#define MACHINE "riscv64"
#endif

static char hostname[NARC_UTS_LENGTH] = "narc";

static void fill(char *field, const char *value) {
    size_t length = strlen(value);
    if (length >= NARC_UTS_LENGTH) length = NARC_UTS_LENGTH - 1;
    memcpy(field, value, length);
    field[length] = 0;
}

long system_uname(uint64_t address) {
    narc_utsname_t info;
    memset(&info, 0, sizeof(info));
    fill(info.sysname, "Narc");
    fill(info.nodename, hostname);
    fill(info.release, NARC_RELEASE);
    fill(info.version, "#1");
    fill(info.machine, MACHINE);
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, address, sizeof(info), 1) ||
        uaccess_write(space, address, &info, sizeof(info)) != 0)
        return -NARC_BAD_ADDRESS;
    return 0;
}

long system_sethostname(uint64_t address, uint64_t length) {
    if (length >= NARC_UTS_LENGTH) return -NARC_INVALID_ARGUMENT;
    char name[NARC_UTS_LENGTH];
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, address, length, 0) ||
        uaccess_read(space, name, address, (size_t)length) != 0)
        return -NARC_BAD_ADDRESS;
    name[length] = 0;
    memcpy(hostname, name, (size_t)length + 1);
    return 0;
}
