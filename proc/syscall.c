#include "syscall.h"
#include "idt.h"
#include "task.h"
#include "vga.h"

static void syscall_handler(registers_t *regs) {
    switch (regs->eax) {
        case SYS_EXIT:
            task_exit(); /* never returns */

        case SYS_WRITE:
            terminal_print((const char *)(uintptr_t)regs->ebx);
            regs->eax = 0;
            break;

        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}

void syscall_init(void) {
    idt_register_handler(0x80, syscall_handler);
}
