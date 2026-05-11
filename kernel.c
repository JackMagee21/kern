#include <stdint.h>
#include <stddef.h>
#include "vga.h"

/* ── Kernel entry point ────────────────────────────────────────────────── */
void kernel_main(void) {
    terminal_init();

    terminal_print("[OK] Print works, Hello world!");

    /* Hang forever — no OS to return to */
    for (;;) {}
}