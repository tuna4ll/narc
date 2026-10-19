# Narc

A small POSIX-like hobby OS for x86_64, AArch64 and RISC-V 64.

```sh
make ARCH=x86_64 run
```

Use `aarch64` or `riscv64` to target another architecture. `run-serial`
attaches the serial console to the terminal instead.

## Features

- preemptive processes with fork, exec and copy-on-write memory
- signals, process groups, sessions and job control
- termios line discipline on a vt100 framebuffer console
- vnode vfs with tmpfs, devfs and ext2 mounts
- virtio block devices over pci and mmio
- `caposh`, a posix shell with job control and line editing

## Disk

The first run creates `build/<arch>/disk.img`, an empty ext2 image
(`DISK_SIZE`, 32M by default). Pablo mounts it on `/mnt` through
`/etc/fstab`, and files written there survive reboots.

## Native userspace ABI

`libnarc` is the native userspace-to-kernel interface. `libc` provides the
standard C and POSIX surface above it. The build installs both into
`build/<arch>/sysroot/usr` and links user programs through that sysroot.

```c
#include <narc/narc.h>

static const char path[] = "/etc/motd";
narc_result_t result = narc_open(path, sizeof(path) - 1, NARC_OPEN_READ, 0);
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

## Shell

`caposh` is installed as `/bin/caposh` and `/bin/sh`. See
`userland/caposh/README.md` for details.
