#include "panic.h"
#include "printf.h"
#include "vga.h"
#include "idt.h"
#include <stdint.h>

/* Human-readable names for the 32 CPU exception vectors. */
static const char *exc_name(uint32_t n) {
    static const char *names[] = {
        "Divide-by-Zero",          /*  0 */
        "Debug",                   /*  1 */
        "Non-Maskable Interrupt",  /*  2 */
        "Breakpoint",              /*  3 */
        "Overflow",                /*  4 */
        "Bound Range Exceeded",    /*  5 */
        "Invalid Opcode",          /*  6 */
        "Device Not Available",    /*  7 */
        "Double Fault",            /*  8 */
        "Coprocessor Overrun",     /*  9 */
        "Invalid TSS",             /* 10 */
        "Segment Not Present",     /* 11 */
        "Stack-Segment Fault",     /* 12 */
        "General Protection Fault",/* 13 */
        "Page Fault",              /* 14 */
        "Reserved",                /* 15 */
        "x87 FP Exception",        /* 16 */
        "Alignment Check",         /* 17 */
        "Machine Check",           /* 18 */
        "SIMD FP Exception",       /* 19 */
        "Virtualisation Exception",/* 20 */
        "Control Protection",      /* 21 */
        "Reserved",                /* 22 */
        "Reserved",                /* 23 */
        "Reserved",                /* 24 */
        "Reserved",                /* 25 */
        "Reserved",                /* 26 */
        "Reserved",                /* 27 */
        "Hypervisor Injection",    /* 28 */
        "VMM Communication",       /* 29 */
        "Security Exception",      /* 30 */
        "Reserved",                /* 31 */
    };
    return (n < 32) ? names[n] : "Unknown";
}

__attribute__((noreturn))
void kpanic(const char *msg, registers_t *regs) {
    __asm__ volatile ("cli");

    terminal_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    terminal_clear();

    terminal_print("\n  *** KERNEL PANIC ***\n\n");
    terminal_print("  ");
    terminal_print(msg);
    terminal_print("\n\n");

    if (regs) {
        kprintf("  Exception %u: %s\n", regs->int_no, exc_name(regs->int_no));
        kprintf("  Error code: 0x%08x\n\n", regs->err_code);

        kprintf("  EAX=%08x  EBX=%08x  ECX=%08x  EDX=%08x\n",
                regs->eax, regs->ebx, regs->ecx, regs->edx);
        kprintf("  ESI=%08x  EDI=%08x  EBP=%08x  ESP=%08x\n",
                regs->esi, regs->edi, regs->ebp, regs->esp);
        kprintf("  EIP=%08x  CS=%04x  EFLAGS=%08x  DS=%04x\n",
                regs->eip, regs->cs, regs->eflags, regs->ds);

        /* Page-fault extras: faulting address + error-code breakdown. */
        if (regs->int_no == 14) {
            uint32_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            kprintf("\n  Faulting address: 0x%08x\n", cr2);
            kprintf("  Fault type: %s %s in %s\n",
                    (regs->err_code & 2) ? "write" : "read",
                    (regs->err_code & 1) ? "(protection violation)"
                                         : "(non-present page)",
                    (regs->err_code & 4) ? "user-mode" : "kernel-mode");
        }
    }

    terminal_print("\n  System halted.\n");

    for (;;) __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}
