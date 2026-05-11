#include "exceptions.h"
#include "idt.h"
#include "panic.h"

/*
 * Single handler for every CPU exception (vectors 0-31).
 * Looks up a human-readable description from the exception number and
 * calls kpanic, which prints the register dump and halts.
 */
static void exc_handler(registers_t *regs) {
    /*
     * Breakpoint (#3) is the one exception we might want to handle
     * gracefully later (e.g. for a debugger).  For now, panic on it too.
     */
    kpanic("CPU exception", regs);
}

void exceptions_init(void) {
    for (uint8_t v = 0; v < 32; v++)
        idt_register_handler(v, exc_handler);
}
