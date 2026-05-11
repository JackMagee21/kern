CC      := i686-elf-gcc
AS      := i686-elf-gcc
LD      := i686-elf-ld

CFLAGS  := -ffreestanding -nostdlib -O2 -Wall -Wextra -std=gnu99 -m32 \
           -Idrivers -Icpu -Ikernel -Imm -Ilib
ASFLAGS := -ffreestanding -nostdlib -m32

BUILD   := build
ISO_DIR := iso
KERNEL  := kern.bin
ISO     := kern.iso

C_SRCS  := $(shell find . -name '*.c' -not -path './$(ISO_DIR)/*')
C_OBJS  := $(patsubst ./%, $(BUILD)/%, $(C_SRCS:.c=.o))
S_SRCS  := $(shell find . -name '*.s' -not -path './$(ISO_DIR)/*')
S_OBJS  := $(patsubst ./%, $(BUILD)/%, $(S_SRCS:.s=.o))
OBJS    := $(S_OBJS) $(C_OBJS)

all: $(ISO)

$(BUILD)/%.o: ./%.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: ./%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(OBJS)

$(ISO): $(KERNEL) grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kern.bin
	cp grub.cfg  $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO)

run-headless: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -nographic -serial stdio

clean:
	rm -rf $(BUILD) $(KERNEL) $(ISO) $(ISO_DIR)/

.PHONY: all run run-headless clean