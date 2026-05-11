
ENTRY(_start)

SECTIONS {
    /*
     * Start at 1 MiB.
     * The first megabyte is reserved for BIOS, VGA buffers, etc.
     */
    . = 1M;

    /* ── Executable code ─────────────────────────────────── */
    .text BLOCK(4K) : ALIGN(4K) {
        *(.multiboot)   /* Multiboot header MUST come first */
        *(.text)
    }

    /* ── Read-only data (constants, string literals) ─────── */
    .rodata BLOCK(4K) : ALIGN(4K) {
        *(.rodata)
    }

    /* ── Initialised read/write data ─────────────────────── */
    .data BLOCK(4K) : ALIGN(4K) {
        *(.data)
    }

    /*
     * ── Uninitialised data (zero-filled at boot) ──────────
     * The .bss section is NOT stored in the binary; the bootloader
     * zeroes this region for us based on the size recorded here.
     */
    .bss BLOCK(4K) : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }
}
