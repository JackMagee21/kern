#include "keyboard.h"
#include "vga.h"
#include "idt.h"
#include "io.h"
#include "pic.h"

#define KB_DATA_PORT 0x60

/* Scan code set 1 — US QWERTY, make codes only (index = scan code). */
static const char sc_ascii[] = {
/*00*/  0,    0,   '1', '2', '3', '4', '5', '6',
/*08*/ '7',  '8', '9', '0', '-', '=',  '\b', '\t',
/*10*/ 'q',  'w', 'e', 'r', 't', 'y', 'u',  'i',
/*18*/ 'o',  'p', '[', ']', '\n', 0,  'a',  's',
/*20*/ 'd',  'f', 'g', 'h', 'j', 'k', 'l',  ';',
/*28*/ '\'', '`', 0,  '\\','z', 'x', 'c',  'v',
/*30*/ 'b',  'n', 'm', ',', '.', '/', 0,    '*',
/*38*/  0,   ' ', 0,    0,   0,   0,   0,    0,
/*40*/  0,    0,   0,   0,   0,   0,   0,   '7',
/*48*/ '8',  '9', '-', '4', '5', '6', '+',  '1',
/*50*/ '2',  '3', '0', '.',
};

#define SC_TABLE_SIZE ((uint8_t)(sizeof(sc_ascii) / sizeof(sc_ascii[0])))

static void keyboard_handler(registers_t *regs) {
    (void)regs;

    uint8_t sc = inb(KB_DATA_PORT);

    /* Bit 7 set = key release (break code) — ignore it. */
    if (sc & 0x80)
        return;

    if (sc < SC_TABLE_SIZE && sc_ascii[sc])
        terminal_putchar(sc_ascii[sc]);
}

void keyboard_init(void) {
    /* IRQ 1 → vector 0x21 */
    idt_register_handler(PIC_IRQ_BASE + 1, keyboard_handler);
}
