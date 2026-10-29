CC := gcc
LD := ld
QEMU := qemu-system-x86_64
XORRISO := xorriso
CURL := curl
GIT := git
BUILD := build

# dış bağımlılıklar, ilk build'de build/ içine indirilip derleniyor
LIMINE_VERSION := 12.9.6
LIMINE_URL := https://github.com/limine-bootloader/limine/releases/download/v$(LIMINE_VERSION)/limine-$(LIMINE_VERSION).tar.gz
FLANTERM_URL := https://github.com/mintsuki/flanterm.git
FLANTERM_REF := v3.1.2

LIMINE_DIR := $(BUILD)/limine-$(LIMINE_VERSION)
LIMINE_BIN := $(LIMINE_DIR)/bin
FLANTERM_DIR := $(BUILD)/flanterm
DEPS_STAMPS := $(LIMINE_DIR)/configure $(FLANTERM_DIR)/.git

# UEFI ile çalıştırmak için (make run-uefi)
OVMF := /usr/share/ovmf/x64/OVMF.4m.fd

# -ffreestanding: libc yok
# -mno-red-zone: interrupt geldiğinde red zone ezilir, kernel'de kapalı olmalı
# -fno-stack-protector: __stack_chk_fail libc'de, bizde yok
# -mcmodel=kernel: kernel en üst 2 GiB'ta (0xffffffff80000000), linker.ld'ye bak
# -fno-tree-loop-distribute-patterns: gcc memset döngüsünü memset çağrısına çevirmesin (mem.c kendini çağırır)
# -MMD -MP: header değişince ilgili .c'ler yeniden derlensin
CFLAGS := -m64 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -fno-lto -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -fno-tree-loop-distribute-patterns
CFLAGS += -Ikernel/include -I$(LIMINE_DIR)/limine-protocol/include -I$(FLANTERM_DIR)/src -MMD -MP

LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.ld

# kernel/ altındaki bütün .c'ler otomatik
KERNEL_SRCS := $(sort $(shell find kernel -name '*.c'))
KERNEL_OBJS := $(patsubst kernel/%.c,$(BUILD)/kernel/%.o,$(KERNEL_SRCS))

FLANTERM_SRCS := $(FLANTERM_DIR)/src/flanterm.c $(FLANTERM_DIR)/src/flanterm_backends/fb.c
FLANTERM_OBJS := $(patsubst $(FLANTERM_DIR)/src/%.c,$(BUILD)/flanterm-obj/%.o,$(FLANTERM_SRCS))

ISO := $(BUILD)/narc.iso
ISO_ROOT := $(BUILD)/iso_root

.PHONY: all deps run run-uefi clean distclean check
all: $(ISO)

deps: $(DEPS_STAMPS) $(LIMINE_BIN)/limine

$(LIMINE_DIR)/configure:
	@mkdir -p $(BUILD)
	$(CURL) -fL -o $(BUILD)/limine-$(LIMINE_VERSION).tar.gz $(LIMINE_URL)
	tar xzf $(BUILD)/limine-$(LIMINE_VERSION).tar.gz -C $(BUILD)
	rm $(BUILD)/limine-$(LIMINE_VERSION).tar.gz
	@touch $@

$(FLANTERM_DIR)/.git:
	$(GIT) clone --branch=$(FLANTERM_REF) --depth=1 $(FLANTERM_URL) $(FLANTERM_DIR)

# sadece x86_64 için lazım olanlar: BIOS (cd + hdd), UEFI x86_64/ia32, UEFI cd imajı
# nasm ve mtools lazım
$(LIMINE_BIN)/limine: | $(LIMINE_DIR)/configure
	cd $(LIMINE_DIR) && ./configure -q --enable-bios --enable-bios-cd --enable-uefi-x86-64 --enable-uefi-ia32 --enable-uefi-cd
	$(MAKE) -C $(LIMINE_DIR)

# header'lar clone'dan geliyor, önce onlar insin (order-only)
$(BUILD)/kernel/%.o: kernel/%.c | $(DEPS_STAMPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(FLANTERM_SRCS): | $(FLANTERM_DIR)/.git

$(BUILD)/flanterm-obj/%.o: $(FLANTERM_DIR)/src/%.c | $(DEPS_STAMPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJS) $(FLANTERM_OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS) $(FLANTERM_OBJS)

# BIOS + UEFI hibrit ISO (limine USAGE.md'deki tarif)
$(ISO): $(BUILD)/kernel.elf boot/limine.conf $(LIMINE_BIN)/limine
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(BUILD)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
	cp boot/limine.conf $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_BIN)/limine-bios.sys $(LIMINE_BIN)/limine-bios-cd.bin $(LIMINE_BIN)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_BIN)/BOOTX64.EFI $(LIMINE_BIN)/BOOTIA32.EFI $(ISO_ROOT)/EFI/BOOT/
	$(XORRISO) -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $@ 2>/dev/null
	$(LIMINE_BIN)/limine bios-install $@

# LC_ALL=C: türkçe locale'de readelf 'Entry point' yazmıyor
check: all
	@echo 'ELF entry:'
	@LC_ALL=C readelf -h $(BUILD)/kernel.elf | grep 'Entry point'
	@echo 'ISO size:'
	@wc -c $(ISO)

run: all
	$(QEMU) -M q35 -m 256M -cdrom $(ISO) -boot d -debugcon stdio -global isa-debugcon.iobase=0xe9

run-uefi: all
	$(QEMU) -M q35 -m 256M -drive if=pflash,unit=0,format=raw,file=$(OVMF),readonly=on -cdrom $(ISO) -boot d -debugcon stdio -global isa-debugcon.iobase=0xe9

# clone'lanan bağımlılıklar kalıyor, her seferinde indirmesin
clean:
	rm -rf $(BUILD)/kernel $(BUILD)/flanterm-obj $(BUILD)/kernel.elf $(ISO_ROOT) $(ISO)

distclean:
	rm -rf $(BUILD)

-include $(KERNEL_OBJS:.o=.d) $(FLANTERM_OBJS:.o=.d)
