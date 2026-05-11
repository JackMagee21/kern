#include <stdint.h>
#include <stddef.h>
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"

void kernel_main(void) {
    terminal_init();

    gdt_init();
    terminal_print("[OK] GDT loaded\n");

    pic_init();
    terminal_print("[OK] PIC remapped\n");

    idt_init();
    terminal_print("[OK] IDT loaded\n");

    __asm__ volatile ("sti"); /* enable interrupts */
    terminal_print("[OK] Interrupts enabled\n");

    terminal_print("\n");
    terminal_print("Hello world!");

    for (;;) {}
}
