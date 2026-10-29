NASM := nasm
CC := gcc
LD := ld
OBJCOPY := objcopy
QEMU := qemu-system-x86_64
BUILD := build

# stage2'nin okuyacağı kernel sektörü (32 * 512 = 16 KiB), nasm'e -D ile gidiyor
KERNEL_SECTORS := 32

NASMFLAGS := -f bin -I boot/include/ -DKERNEL_SECTORS=$(KERNEL_SECTORS)
BOOT_INCLUDES := $(wildcard boot/include/*.inc)

# -ffreestanding: libc yok
# -mno-red-zone: interrupt geldiğinde red zone ezilir, kernel'de kapalı olmalı
# -fno-stack-protector: __stack_chk_fail libc'de, bizde yok
# -MMD -MP: header değişince ilgili .c'ler yeniden derlensin
CFLAGS := -m64 -std=c11 -O2 -Wall -Wextra -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only -mcmodel=small
CFLAGS += -Ikernel/include -MMD -MP

# kernel/ altındaki bütün .c'ler otomatik
KERNEL_SRCS := $(sort $(shell find kernel -name '*.c'))
KERNEL_OBJS := $(patsubst kernel/%.c,$(BUILD)/kernel/%.o,$(KERNEL_SRCS))

.PHONY: all run clean check
all: $(BUILD)/os.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.bin: boot/boot.asm $(BOOT_INCLUDES) | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@
	@test $$(wc -c < $@) -eq 512

$(BUILD)/stage2.bin: boot/stage2.asm $(BOOT_INCLUDES) | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@
	@test $$(wc -c < $@) -eq 2048

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# obj sırası önemli değil, kmain'i linker.ld başa koyuyor
$(BUILD)/kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	$(LD) -m elf_x86_64 -nostdlib -T kernel/linker.ld -o $@ $(KERNEL_OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@test $$(wc -c < $@) -le $$(( $(KERNEL_SECTORS) * 512 )) || (echo 'Kernel exceeds 16KiB raw load limit'; exit 1)

# 1.44 MB boş imaj, sonra LBA 0 / 1 / 5'e parçalar (config.inc'teki düzen)
$(BUILD)/os.img: $(BUILD)/boot.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/boot.bin of=$@ bs=512 seek=0 conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=5 conv=notrunc status=none

# LC_ALL=C: türkçe locale'de readelf 'Entry point' yazmıyor
check: all
	@echo 'Boot signature:'
	@od -An -tx1 -j510 -N2 $(BUILD)/boot.bin
	@echo 'ELF entry:'
	@LC_ALL=C readelf -h $(BUILD)/kernel.elf | grep 'Entry point'
	@echo 'Image size:'
	@wc -c $(BUILD)/os.img

# disket değil ide, EDD okuması HDD istiyor
run: all
	$(QEMU) -drive file=$(BUILD)/os.img,format=raw,if=ide -boot c -debugcon stdio -global isa-debugcon.iobase=0xe9

clean:
	rm -rf $(BUILD)

-include $(KERNEL_OBJS:.o=.d)
