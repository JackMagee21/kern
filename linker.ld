
KERNEL_VMA = 0xC0000000;
ENTRY(_start)

SECTIONS {
    /*
     * Boot trampoline: VMA = LMA = 0x100000.
     * GRUB jumps to _start here, before paging is on.
     * The multiboot header must be in the first 8 KB of the file.
     */
    . = 0x100000;
    .boot : {
        *(.multiboot)
        *(.boot)
    }

    /*
     * Kernel proper: VMA = 0xC0101000, LMA = 0x101000.
     * AT() sets the physical load address so GRUB copies each section
     * to LMA = VMA - KERNEL_VMA.
     */
    . = KERNEL_VMA + 0x101000;

    .text ALIGN(4096) : AT(ADDR(.text) - KERNEL_VMA) {
        *(.text*)
    }

    .rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNEL_VMA) {
        *(.rodata*)
    }

    .data ALIGN(4096) : AT(ADDR(.data) - KERNEL_VMA) {
        *(.data*)
    }

    .bss ALIGN(4096) : AT(ADDR(.bss) - KERNEL_VMA) {
        *(COMMON)
        *(.bss*)
    }

    _kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
