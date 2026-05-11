#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

/*
 * Drop into ring-3 by building an iret frame.
 * fn           — entry point for the user code (must be in a VMM_USER page)
 * user_stack_top — top of the pre-allocated user stack
 *
 * TSS.esp0 is updated to the top of the current task's kernel stack before
 * the privilege switch, so ring-3→ring-0 transitions use the right stack.
 * Never returns.
 */
__attribute__((noreturn))
void enter_usermode(void (*fn)(void), uint32_t user_stack_top);

#endif
