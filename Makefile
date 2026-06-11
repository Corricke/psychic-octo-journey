# OctoOS build
#
# Produces os.img, a 16 MiB disk:
#   LBA 0          boot sector (with MBR partition table)
#   LBA 1-64       kernel (KERNEL_SECTORS, must match boot/boot.asm)
#   LBA 2048-      FAT16 partition (15 MiB, must match the MBR entry)

KERNEL_SECTORS := 128
FAT_SECTORS    := 30720
FAT_START      := 2048

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
OBJS    := $(BUILD)/entry.o $(BUILD)/isr.o $(BUILD)/ctx.o $(C_OBJS)

USER_PROGS := hello ticker greet
USER_BINS  := $(patsubst %,$(BUILD)/user/%.bin,$(USER_PROGS))

.PHONY: all run run-vga clean

all: os.img

os.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin $(BUILD)/fat.img
	@size=$$(stat -c%s $(BUILD)/kernel.bin); \
	max=$$(( $(KERNEL_SECTORS) * 512 )); \
	if [ $$size -gt $$max ]; then \
	    echo "kernel.bin is $$size bytes, exceeds $$max (bump KERNEL_SECTORS)"; \
	    exit 1; \
	fi
	cat $(BUILD)/boot.bin $(BUILD)/kernel.bin > $@
	truncate -s $$(( $(FAT_START) * 512 )) $@
	cat $(BUILD)/fat.img >> $@

# FAT16 filesystem populated with starter files and user programs.
$(BUILD)/fat.img: README.md $(USER_BINS) | $(BUILD)
	truncate -s $$(( $(FAT_SECTORS) * 512 )) $@.tmp
	mkfs.fat -F 16 -n OCTOOS $@.tmp > /dev/null
	echo "Hello from the OctoOS filesystem!" > $(BUILD)/hello.txt
	seq -f "line %.0f of a file big enough to span several clusters" 1 700 \
	    > $(BUILD)/lorem.txt
	mcopy -i $@.tmp README.md ::README.MD
	mcopy -i $@.tmp $(BUILD)/hello.txt ::HELLO.TXT
	mcopy -i $@.tmp $(BUILD)/lorem.txt ::LOREM.TXT
	mmd -i $@.tmp ::BIN
	for p in $(USER_PROGS); do \
	    mcopy -i $@.tmp $(BUILD)/user/$$p.bin ::BIN/ ; \
	done
	mv $@.tmp $@

# User programs: flat binaries linked at USER_BASE.
$(BUILD)/user/%.o: user/%.c | $(BUILD)
	@mkdir -p $(BUILD)/user
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/user/%.bin: $(BUILD)/user/crt0.o $(BUILD)/user/%.o user/user.ld
	$(LD) -m elf_i386 -T user/user.ld --oformat binary -nostdlib \
	    $(BUILD)/user/crt0.o $(BUILD)/user/$*.o -o $@

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
