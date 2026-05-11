#include <stdint.h>
#include <stddef.h>
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "timer.h"
#include "printf.h"

void kernel_main(void) {
    terminal_init();

    gdt_init();
    terminal_print("[OK] GDT loaded\n");

    pic_init();
    terminal_print("[OK] PIC remapped\n");

    idt_init();
    terminal_print("[OK] IDT loaded\n");

    keyboard_init();
    terminal_print("[OK] Keyboard ready\n");

    timer_init(1000); /* 1000 Hz — one tick per millisecond */
    terminal_print("[OK] Timer ready\n");

    __asm__ volatile ("sti");
    terminal_print("[OK] Interrupts enabled\n\n");

    kprintf("Ticks so far: %u\n", timer_get_ticks());
    kprintf("Type something: ");

    for (;;) {}
}
