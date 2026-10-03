ARCH ?= x86_64

BUILD := build/$(ARCH)
DIST := dist/$(ARCH)
CACHE := .cache
SYSROOT := $(BUILD)/sysroot
SYSROOT_INCLUDE := $(SYSROOT)/usr/include
SYSROOT_LIB := $(SYSROOT)/usr/lib
SYSROOT_STAMP := $(SYSROOT)/.installed
USER_APP := $(BUILD)/userland/init
PABLO_C := $(shell find userland/pablo/src -name '*.c' 2>/dev/null | sort)
PABLO_OBJ := $(patsubst userland/pablo/%.c,$(BUILD)/userland/pablo/%.o,$(PABLO_C))
PABLO_HEADERS := $(shell find userland/pablo/include -type f 2>/dev/null | sort)
INITRAMFS := $(BUILD)/initramfs.tar
INITRAMFS_ROOT := $(BUILD)/initramfs_root
USER_BASE := 0x400000
ROOTFS_FILES := $(shell find userland/rootfs -type f 2>/dev/null | sort)

COMMON_CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding \
                 -fno-stack-protector -fno-pic -fno-pie -I kernel/include -I include
COMMON_USER_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
                      -fno-stack-protector -fno-pic -fno-pie
COMMON_USER_LDFLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 \
                       -Wl,--build-id=none -Wl,-e,_start
LIBNARC_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
                  -fno-stack-protector -I include -I lib/libnarc/include
LIBC_CFLAGS := $(COMMON_USER_CFLAGS) -fno-builtin -I lib/libc/include \
               -I lib/libnarc/include -I include

ifeq ($(ARCH),x86_64)
KCC := cc
KLD := ld
KERNEL_CFLAGS := -m64 -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2
KERNEL_ASFLAGS := -m64 -mno-red-zone -mcmodel=kernel
USER_ARCH_FLAGS := -march=x86-64 -mtune=generic
USER_LINK_FLAGS := -no-pie -Wl,-Ttext-segment=$(USER_BASE)
USER_CC := cc
USER_AR := ar
EFI_BOOT := BOOTX64.EFI
ISO_BIOS_FLAGS := -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table
else ifeq ($(ARCH),aarch64)
KCC := aarch64-linux-gnu-gcc
KLD := aarch64-linux-gnu-ld
KERNEL_CFLAGS := -mgeneral-regs-only -mstrict-align
KERNEL_ASFLAGS :=
USER_ARCH_FLAGS := -march=armv8-a -fno-link-libatomic
USER_LINK_FLAGS := -no-pie -Wl,-Ttext-segment=$(USER_BASE)
USER_CC := aarch64-linux-gnu-gcc
USER_AR := aarch64-linux-gnu-ar
EFI_BOOT := BOOTAA64.EFI
else ifeq ($(ARCH),riscv64)
KCC := riscv64-linux-gnu-gcc
KLD := riscv64-linux-gnu-ld
KERNEL_CFLAGS := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax -msmall-data-limit=0
KERNEL_ASFLAGS := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax -msmall-data-limit=0
USER_ARCH_FLAGS := -march=rv64gc_zicsr_zifencei -mabi=lp64d \
                   -msmall-data-limit=0 -fno-link-libatomic
USER_LINK_FLAGS := -no-pie -Wl,-Ttext-segment=$(USER_BASE)
USER_CC := riscv64-linux-gnu-gcc
USER_AR := riscv64-linux-gnu-ar
EFI_BOOT := BOOTRISCV64.EFI
else
$(error unsupported ARCH: $(ARCH))
endif

CFLAGS := $(COMMON_CFLAGS) $(KERNEL_CFLAGS)
ASFLAGS := -ffreestanding -fno-pic -fno-pie $(KERNEL_ASFLAGS)
USER_CFLAGS := $(COMMON_USER_CFLAGS) $(USER_ARCH_FLAGS) $(USER_LINK_FLAGS) \
               --sysroot=$(abspath $(SYSROOT)) -nostdinc \
               -isystem $(SYSROOT_INCLUDE) -isystem $(shell $(USER_CC) -print-file-name=include)
USER_LDFLAGS := $(COMMON_USER_LDFLAGS) $(USER_LINK_FLAGS)
LINKER := kernel/arch/$(ARCH)/linker.ld
LIBNARC_COMMON_C := $(shell find lib/libnarc/src -name '*.c' | sort)
LIBNARC_ARCH_C := lib/libnarc/arch/$(ARCH)/syscall.c
LIBNARC_C := $(LIBNARC_COMMON_C) $(LIBNARC_ARCH_C)
LIBNARC_OBJ := $(patsubst lib/libnarc/%.c,$(BUILD)/lib/libnarc/%.o,$(LIBNARC_C))
LIBNARC := $(BUILD)/lib/libnarc.a
LIBC_C := $(shell find lib/libc/src -name '*.c' | sort)
LIBC_OBJ := $(patsubst lib/libc/%.c,$(BUILD)/lib/libc/%.o,$(LIBC_C))
LIBC_CRT0 := $(BUILD)/lib/libc/crt0.o
LIBC_CRT1 := $(BUILD)/lib/libc/crt1.o
LIBC := $(BUILD)/lib/libc.a
LIBC_HEADERS := $(shell find lib/libc/include -type f | sort)
LIBNARC_HEADERS := $(shell find lib/libnarc/include -type f | sort)
UAPI_HEADERS := $(shell find include/narc -type f | sort)

COMMON_KERNEL_C := $(shell find kernel -path kernel/arch -prune -o -name '*.c' -print | sort)
COMMON_KERNEL_S := $(shell find kernel -path kernel/arch -prune -o -name '*.S' -print | sort)
ARCH_KERNEL_C := $(shell find kernel/arch/$(ARCH) -name '*.c' | sort)
ARCH_KERNEL_S := $(shell find kernel/arch/$(ARCH) -name '*.S' | sort)
KERNEL_C := $(COMMON_KERNEL_C) $(ARCH_KERNEL_C)
KERNEL_S := $(COMMON_KERNEL_S) $(ARCH_KERNEL_S)
KERNEL_O := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_C)) \
            $(patsubst %.S,$(BUILD)/%.o,$(KERNEL_S))

.PHONY: all kernel libnarc libc sysroot userland iso run run-serial clean distclean
all: iso

include recipes/limine/RECIPE
include recipes/edk2/RECIPE

$(BUILD)/lib/libnarc/%.o: lib/libnarc/%.c include/narc/abi.h \
                         lib/libnarc/include/narc/narc.h
	@mkdir -p $(dir $@)
	$(USER_CC) $(LIBNARC_CFLAGS) $(USER_ARCH_FLAGS) -c $< -o $@

$(LIBNARC): $(LIBNARC_OBJ)
	@mkdir -p $(dir $@)
	$(RM) $@
	$(USER_AR) rcs $@ $^

libnarc: $(LIBNARC)

$(BUILD)/lib/libc/%.o: lib/libc/%.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(LIBC_CFLAGS) $(USER_ARCH_FLAGS) -c $< -o $@

$(LIBC_CRT0): lib/libc/crt/arch/$(ARCH)/crt0.S
	@mkdir -p $(dir $@)
	$(USER_CC) -ffreestanding -fno-pic -fno-pie $(USER_ARCH_FLAGS) -c $< -o $@

$(LIBC_CRT1): lib/libc/crt/crt1.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(LIBC_CFLAGS) $(USER_ARCH_FLAGS) -c $< -o $@

$(LIBC): $(LIBC_OBJ)
	@mkdir -p $(dir $@)
	$(RM) $@
	$(USER_AR) rcs $@ $^

libc: $(LIBC) $(LIBC_CRT0) $(LIBC_CRT1)

$(SYSROOT_STAMP): $(LIBC) $(LIBNARC) $(LIBC_CRT0) $(LIBC_CRT1) \
                  $(LIBC_HEADERS) $(LIBNARC_HEADERS) $(UAPI_HEADERS)
	rm -rf $(SYSROOT)
	mkdir -p $(SYSROOT_INCLUDE) $(SYSROOT_LIB)
	cp -R lib/libc/include/. $(SYSROOT_INCLUDE)/
	cp -R lib/libnarc/include/. $(SYSROOT_INCLUDE)/
	cp -R include/narc $(SYSROOT_INCLUDE)/
	cp $(LIBC) $(SYSROOT_LIB)/libc.a
	cp $(LIBNARC) $(SYSROOT_LIB)/libnarc.a
	cp $(LIBC_CRT0) $(SYSROOT_LIB)/crt0.o
	cp $(LIBC_CRT1) $(SYSROOT_LIB)/crt1.o
	touch $@

sysroot: $(SYSROOT_STAMP)

$(BUILD)/userland/pablo/%.o: userland/pablo/%.c $(SYSROOT_STAMP) $(PABLO_HEADERS)
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -I userland/pablo/include -c $< -o $@

$(USER_APP): $(PABLO_OBJ) $(SYSROOT_STAMP)
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) $(SYSROOT_LIB)/crt0.o $(SYSROOT_LIB)/crt1.o $(PABLO_OBJ) \
		-L$(SYSROOT_LIB) -lc -lnarc $(USER_LDFLAGS) -o $@

userland: $(USER_APP)

$(INITRAMFS): $(USER_APP) $(ROOTFS_FILES)
	rm -rf $(INITRAMFS_ROOT)
	mkdir -p $(INITRAMFS_ROOT)/sbin $(INITRAMFS_ROOT)/bin
	cp -R userland/rootfs/. $(INITRAMFS_ROOT)/
	cp $(USER_APP) $(INITRAMFS_ROOT)/sbin/init
	cp $(USER_APP) $(INITRAMFS_ROOT)/sbin/pablo
	cp $(USER_APP) $(INITRAMFS_ROOT)/bin/init
	tar --format=ustar --owner=0 --group=0 --numeric-owner -cf $@ -C $(INITRAMFS_ROOT) .

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(KCC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_O) $(LINKER)
	$(KLD) -nostdlib -static -z max-page-size=0x1000 -T $(LINKER) $(KERNEL_O) -o $@

kernel: $(BUILD)/kernel.elf

iso: $(BUILD)/kernel.elf $(INITRAMFS) $(LIMINE_TOOL)
	rm -rf $(BUILD)/iso_root $(DIST)
	mkdir -p $(BUILD)/iso_root/boot/limine $(BUILD)/iso_root/EFI/BOOT $(DIST)
	cp $(BUILD)/kernel.elf $(BUILD)/iso_root/boot/kernel.elf
	cp $(INITRAMFS) $(BUILD)/iso_root/boot/initramfs.tar
	cp limine.conf $(BUILD)/iso_root/boot/limine/limine.conf
	cp $(LIMINE_SRC)/limine-uefi-cd.bin $(BUILD)/iso_root/boot/limine/
	cp $(LIMINE_SRC)/$(EFI_BOOT) $(BUILD)/iso_root/EFI/BOOT/
	$(if $(filter x86_64,$(ARCH)),cp $(LIMINE_SRC)/limine-bios.sys $(LIMINE_SRC)/limine-bios-cd.bin $(BUILD)/iso_root/boot/limine/)
	xorriso -as mkisofs -R -r -J $(ISO_BIOS_FLAGS) \
		-hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD)/iso_root -o $(DIST)/narc-$(ARCH).iso
	$(if $(filter x86_64,$(ARCH)),$(LIMINE_TOOL) bios-install $(DIST)/narc-$(ARCH).iso)

ifeq ($(ARCH),x86_64)
run run-serial: iso
	qemu-system-x86_64 -M q35 -m 256M -vga std -cdrom $(DIST)/narc-$(ARCH).iso \
		-serial $(if $(filter run-serial,$@),stdio,none) -monitor none -no-reboot -no-shutdown
else ifeq ($(ARCH),aarch64)
run run-serial: iso $(EDK2_PREPARED)
	cp $(EDK2_DIR)/ovmf-vars-aarch64.fd $(BUILD)/ovmf-vars.fd
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256M -device ramfb \
		-drive if=pflash,unit=0,format=raw,file=$(EDK2_DIR)/ovmf-code-aarch64.fd,readonly=on \
		-drive if=pflash,unit=1,format=raw,file=$(BUILD)/ovmf-vars.fd \
		-cdrom $(DIST)/narc-$(ARCH).iso -serial $(if $(filter run-serial,$@),stdio,none) \
		-monitor none -no-reboot -no-shutdown
else
run run-serial: iso $(EDK2_PREPARED)
	cp $(EDK2_DIR)/ovmf-vars-riscv64.fd $(BUILD)/ovmf-vars.fd
	qemu-system-riscv64 -M virt -cpu rv64 -m 256M -device ramfb \
		-drive if=pflash,unit=0,format=raw,file=$(EDK2_DIR)/ovmf-code-riscv64.fd,readonly=on \
		-drive if=pflash,unit=1,format=raw,file=$(BUILD)/ovmf-vars.fd \
		-cdrom $(DIST)/narc-$(ARCH).iso -serial $(if $(filter run-serial,$@),stdio,none) \
		-monitor none -no-reboot -no-shutdown
endif

clean:
	rm -rf build dist

distclean: clean
	rm -rf third_party $(CACHE)
