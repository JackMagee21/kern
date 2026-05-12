CC      := i686-elf-gcc
AS      := i686-elf-gcc
LD      := i686-elf-ld

CFLAGS  := -ffreestanding -nostdlib -O2 -Wall -Wextra -std=gnu99 -m32 \
           -Idrivers -Icpu -Ikernel -Imm -Ilib -Iproc -Ifs
ASFLAGS := -ffreestanding -nostdlib -m32

BUILD   := build
ISO_DIR := iso
KERNEL  := kern.bin
ISO     := kern.iso

INITRD      := initrd.img
ULAND_ELF   := userland/test.elf
ULAND_LD    := userland/user.ld
UCFLAGS     := -ffreestanding -nostdlib -O2 -std=gnu99 -m32

# ── Kernel sources ──────────────────────────────────────────────────────
C_SRCS  := $(shell find . -name '*.c' \
               -not -path './$(ISO_DIR)/*' \
               -not -path './userland/*')
C_OBJS  := $(patsubst ./%, $(BUILD)/%, $(C_SRCS:.c=.o))

S_SRCS  := $(shell find . -name '*.s' \
               -not -path './$(ISO_DIR)/*' \
               -not -path './userland/*')
S_OBJS  := $(patsubst ./%, $(BUILD)/%, $(S_SRCS:.s=.o))

OBJS    := $(S_OBJS) $(C_OBJS)

# ── Top-level targets ───────────────────────────────────────────────────
all: $(ISO)

# ── Kernel object compilation ───────────────────────────────────────────
$(BUILD)/%.o: ./%.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: ./%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Kernel binary ───────────────────────────────────────────────────────
$(KERNEL): $(OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(OBJS)

# ── User-land ELF ───────────────────────────────────────────────────────
$(ULAND_ELF): userland/test.c $(ULAND_LD)
	$(CC) $(UCFLAGS) -T$(ULAND_LD) -o $@ userland/test.c

# ── Initrd image ────────────────────────────────────────────────────────
$(INITRD): $(ULAND_ELF) tools/mkinitrd.py
	python3 tools/mkinitrd.py $@ $(ULAND_ELF):test

# ── ISO image ───────────────────────────────────────────────────────────
$(ISO): $(KERNEL) $(INITRD) grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kern.bin
	cp $(INITRD) $(ISO_DIR)/boot/initrd.img
	cp grub.cfg  $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

# ── Run targets ─────────────────────────────────────────────────────────
run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -serial stdio -display gtk

run-headless: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -display none -serial stdio

run-debug: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -display none -serial stdio \
	    -d int,cpu_reset -no-reboot -no-shutdown 2>qemu-debug.log

# ── Clean ───────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD) $(KERNEL) $(ISO) $(ISO_DIR)/ qemu-debug.log \
	       $(INITRD) $(ULAND_ELF)

.PHONY: all run run-headless run-debug clean
