# narcOs

A small POSIX-like hobby OS for x86_64, AArch64 and RISC-V 64.

```sh
make ARCH=x86_64 run
```

Use `aarch64` or `riscv64` to target another architecture.

## Native userspace ABI

`libnarc` is the native userspace-to-kernel interface. `libc` provides the
standard C and POSIX surface above it. The build installs both into
`build/<arch>/sysroot/usr` and links user programs through that sysroot.

```c
#include <narcos/narc.h>

static const char path[] = "/etc/motd";
narc_result_t result = narc_open(path, sizeof(path) - 1, NARC_OPEN_READ);
if (result.status != NARC_OK) {
    return 1;
}
```

Native syscall IDs are architecture-independent. Calls return their value and
status in separate registers, represented by `narc_result_t`. User programs
link with the project-owned `libc.a` and `libnarc.a` archives.

## Init system

Pablo is the PID 1 service supervisor. Its sources are split by responsibility
under `userland/pablo`; service units live in `/etc/pablo/services`. Pablo
supports ordered one-shot services, supervised services, restart backoff and
orphan reaping.
