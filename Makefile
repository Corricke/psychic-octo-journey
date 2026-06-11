# OctoOS build
#
# Produces os.img: [boot sector][kernel padded to KERNEL_SECTORS sectors].
# KERNEL_SECTORS must match boot/boot.asm.

KERNEL_SECTORS := 64

CC      := gcc
LD      := ld
NASM    := nasm
QEMU    := qemu-system-i386

CFLAGS  := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
           -mno-mmx -mno-sse -mno-sse2 -mno-80387 \
           --param min-pagesize=0 \
           -Wall -Wextra -O2 -g -MMD
LDFLAGS := -m elf_i386 -T kernel/linker.ld --oformat binary -nostdlib

BUILD   := build

C_SRCS  := $(wildcard kernel/*.c)
C_OBJS  := $(patsubst kernel/%.c,$(BUILD)/%.o,$(C_SRCS))
OBJS    := $(BUILD)/entry.o $(BUILD)/isr.o $(C_OBJS)

.PHONY: all run run-vga clean

all: os.img

os.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	@size=$$(stat -c%s $(BUILD)/kernel.bin); \
	max=$$(( $(KERNEL_SECTORS) * 512 )); \
	if [ $$size -gt $$max ]; then \
	    echo "kernel.bin is $$size bytes, exceeds $$max (bump KERNEL_SECTORS)"; \
	    exit 1; \
	fi
	cat $(BUILD)/boot.bin $(BUILD)/kernel.bin > $@
	truncate -s $$(( ($(KERNEL_SECTORS) + 1) * 512 )) $@

$(BUILD)/boot.bin: boot/boot.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/kernel.bin: $(OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: kernel/%.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: os.img
	$(QEMU) -drive format=raw,file=os.img -display none -serial stdio

run-vga: os.img
	$(QEMU) -drive format=raw,file=os.img -serial stdio

clean:
	rm -rf $(BUILD) os.img

-include $(BUILD)/*.d
