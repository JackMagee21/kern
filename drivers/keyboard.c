#include "keyboard.h"
#include "task.h"
#include "signal.h"
#include "idt.h"
#include "io.h"
#include "pic.h"
#include <stdint.h>

#define KB_DATA_PORT 0x60
#define KB_BUF_SIZE  256

static volatile char     kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0;
static volatile uint32_t kb_tail = 0;

/* Modifier state */
static volatile uint8_t shift_held = 0;
static volatile uint8_t ctrl_held  = 0;
static volatile uint8_t ext_prefix = 0; /* set when 0xE0 prefix seen */

/* Unshifted scan-code → ASCII (scan-code set 1) */
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

/* Shifted scan-code → ASCII */
static const char sc_shift[] = {
/*00*/  0,    0,   '!', '@', '#', '$', '%', '^',
/*08*/ '&',  '*', '(', ')', '_', '+', '\b', '\t',
/*10*/ 'Q',  'W', 'E', 'R', 'T', 'Y', 'U',  'I',
/*18*/ 'O',  'P', '{', '}', '\n', 0,  'A',  'S',
/*20*/ 'D',  'F', 'G', 'H', 'J', 'K', 'L',  ':',
/*28*/ '"',  '~', 0,  '|', 'Z', 'X', 'C',  'V',
/*30*/ 'B',  'N', 'M', '<', '>', '?', 0,    '*',
/*38*/  0,   ' ', 0,    0,   0,   0,   0,    0,
/*40*/  0,    0,   0,   0,   0,   0,   0,   '7',
/*48*/ '8',  '9', '-', '4', '5', '6', '+',  '1',
/*50*/ '2',  '3', '0', '.',
};

#define SC_TABLE_SIZE ((uint8_t)(sizeof(sc_ascii) / sizeof(sc_ascii[0])))

static void kb_put(char c) {
    uint32_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

static void keyboard_handler(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(KB_DATA_PORT);

    /* Extended-key prefix byte */
    if (sc == 0xE0) { ext_prefix = 1; return; }

    /* Key-release event */
    if (sc & 0x80) {
        uint8_t key = sc & 0x7F;
        if (key == 0x2A || key == 0x36) shift_held = 0;
        if (key == 0x1D)               ctrl_held  = 0;
        ext_prefix = 0;
        return;
    }

    /* Modifier key presses */
    if (sc == 0x2A || sc == 0x36) { shift_held = 1; ext_prefix = 0; return; }
    if (sc == 0x1D)               { ctrl_held  = 1; ext_prefix = 0; return; }

    /* Extended keys: emit ANSI escape sequences for arrow keys */
    if (ext_prefix) {
        ext_prefix = 0;
        switch (sc) {
        case 0x48: kb_put('\x1b'); kb_put('['); kb_put('A'); return; /* Up    */
        case 0x50: kb_put('\x1b'); kb_put('['); kb_put('B'); return; /* Down  */
        case 0x4D: kb_put('\x1b'); kb_put('['); kb_put('C'); return; /* Right */
        case 0x4B: kb_put('\x1b'); kb_put('['); kb_put('D'); return; /* Left  */
        }
        return;
    }

    if (sc >= SC_TABLE_SIZE) return;

    /* Ctrl+C → deliver SIGINT to the foreground task */
    if (ctrl_held && sc == 0x2E) { /* scan 0x2E = 'c' */
        task_t *fg = task_get_fg();
        if (!fg) fg = task_current();
        task_signal(fg, SIGINT);
        return;
    }

    /* Ctrl+Z → deliver SIGTSTP to the foreground task */
    if (ctrl_held && sc == 0x2C) { /* scan 0x2C = 'z' */
        task_t *fg = task_get_fg();
        if (!fg) fg = task_current();
        task_signal(fg, SIGTSTP);
        return;
    }

    char c = shift_held ? sc_shift[sc] : sc_ascii[sc];
    if (!c) return;
    kb_put(c);
}

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
