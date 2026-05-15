CC      := i686-elf-gcc
AS      := i686-elf-gcc
LD      := i686-elf-ld

CFLAGS  := -ffreestanding -nostdlib -O2 -Wall -Wextra -std=gnu99 -m32 \
           -Idrivers -Icpu -Ikernel -Imm -Ilib -Iproc -Ifs \
           -MMD -MP
ASFLAGS := -ffreestanding -nostdlib -m32

BUILD    := build
ISO_DIR  := iso
KERNEL   := kern.bin
ISO      := kern.iso
DISK_IMG := disk.img

INITRD      := initrd.img
ULAND_LD    := userland/user.ld
UCFLAGS     := -ffreestanding -nostdlib -O2 -std=gnu99 -m32 -Wno-builtin-declaration-mismatch

ULAND_ELFS  := userland/sh.elf   \
               userland/echo.elf \
               userland/cat.elf  \
               userland/ls.elf   \
               userland/wc.elf   \
               userland/grep.elf \
               userland/test.elf

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

# ── User-land ELFs ──────────────────────────────────────────────────────
userland/%.elf: userland/%.c $(ULAND_LD)
	$(CC) $(UCFLAGS) -T$(ULAND_LD) -o $@ $<

# ── Initrd image ────────────────────────────────────────────────────────
$(INITRD): $(ULAND_ELFS) tools/mkinitrd.py
	python3 tools/mkinitrd.py $@   \
	    userland/sh.elf:sh         \
	    userland/echo.elf:echo     \
	    userland/cat.elf:cat       \
	    userland/ls.elf:ls         \
	    userland/wc.elf:wc         \
	    userland/grep.elf:grep     \
	    userland/test.elf:test

# ── ISO image ───────────────────────────────────────────────────────────
$(ISO): $(KERNEL) $(INITRD) grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kern.bin
	cp $(INITRD) $(ISO_DIR)/boot/initrd.img
	cp grub.cfg  $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

# ── Disk image (FAT16, 32 MB) — created once; not deleted by 'clean' ────
$(DISK_IMG):
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=32 2>/dev/null
	mkfs.fat -F 16 $(DISK_IMG) >/dev/null

# ── Run targets ─────────────────────────────────────────────────────────
QEMU_DRIVE := -drive file=$(DISK_IMG),format=raw,if=ide,index=0 -boot order=d

run: $(ISO) $(DISK_IMG)
	qemu-system-i386 -cdrom $(ISO) -serial stdio -display gtk $(QEMU_DRIVE)

run-headless: $(ISO) $(DISK_IMG)
	qemu-system-i386 -cdrom $(ISO) -display none -serial stdio $(QEMU_DRIVE)

run-debug: $(ISO) $(DISK_IMG)
	qemu-system-i386 -cdrom $(ISO) -display none -serial stdio \
	    -d int,cpu_reset -no-reboot -no-shutdown $(QEMU_DRIVE) 2>qemu-debug.log

# ── Clean ───────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD) $(KERNEL) $(ISO) $(ISO_DIR)/ qemu-debug.log \
	       $(INITRD) $(ULAND_ELFS)

clean-disk:
	rm -f $(DISK_IMG)

# ── Auto-generated header dependencies ─────────────────────────────────────
-include $(C_OBJS:.o=.d)

.PHONY: all run run-headless run-debug clean clean-disk
