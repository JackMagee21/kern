#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "pic.h"
#include <stdint.h>

#define KB_DATA_PORT 0x60
#define KB_BUF_SIZE  256

static volatile char     kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0; /* write index (IRQ handler) */
static volatile uint32_t kb_tail = 0; /* read  index (shell)       */

static const char sc_ascii[] = {
/*00*/  0,    0,   '1', '2', '3', '4', '5', '6',
/*08*/ '7',  '8', '9', '0', '-', '=', '\b', '\t',
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
    if (sc & 0x80) return; /* key release — ignore */

    if (sc >= SC_TABLE_SIZE || !sc_ascii[sc]) return;

    uint32_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) { /* drop if buffer full */
        kb_buf[kb_head] = sc_ascii[sc];
        kb_head = next;
    }
}

/* Spin-halt until a character arrives, then return it. */
char keyboard_getchar(void) {
    while (kb_head == kb_tail)
        __asm__ volatile ("hlt");
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

void keyboard_init(void) {
    idt_register_handler(PIC_IRQ_BASE + 1, keyboard_handler);
}
