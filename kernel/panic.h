#ifndef PANIC_H
#define PANIC_H

#include "idt.h"

/*
 * Print a red-screen register dump and halt permanently.
 * msg   : short description (e.g. the exception name).
 * regs  : register snapshot from the ISR stub, or NULL.
 * Never returns.
 */
__attribute__((noreturn))
void kpanic(const char *msg, registers_t *regs);

#endif
