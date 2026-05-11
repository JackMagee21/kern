#include "idt.h"
#include "pic.h"

#define IDT_ENTRIES 256

/* Interrupt gate: present, ring 0, 32-bit interrupt gate type (0xE). */
#define IDT_GATE_INT32_RING0 0x8E

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

static isr_handler_t handlers[IDT_ENTRIES];

/* Declare all stubs from isr.s */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void idt_set_gate(uint8_t vec, void (*handler)(void)) {
    uint32_t addr = (uint32_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].offset_high = (addr >> 16) & 0xFFFF;
    idt[vec].selector    = 0x08;
    idt[vec].zero        = 0;
    idt[vec].type_attr   = IDT_GATE_INT32_RING0;
}

/* Called from isr_common in isr.s */
void isr_dispatch(registers_t *regs) {
    isr_handler_t h = handlers[regs->int_no];
    if (h)
        h(regs);

    /* Send EOI for hardware IRQs (vectors 0x20-0x2F). */
    if (regs->int_no >= PIC_IRQ_BASE && regs->int_no < PIC_IRQ_BASE + 16)
        pic_send_eoi(regs->int_no - PIC_IRQ_BASE);
}

void idt_register_handler(uint8_t vector, isr_handler_t handler) {
    handlers[vector] = handler;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    /* Exceptions */
    idt_set_gate(0,  isr0);  idt_set_gate(1,  isr1);  idt_set_gate(2,  isr2);
    idt_set_gate(3,  isr3);  idt_set_gate(4,  isr4);  idt_set_gate(5,  isr5);
    idt_set_gate(6,  isr6);  idt_set_gate(7,  isr7);  idt_set_gate(8,  isr8);
    idt_set_gate(9,  isr9);  idt_set_gate(10, isr10); idt_set_gate(11, isr11);
    idt_set_gate(12, isr12); idt_set_gate(13, isr13); idt_set_gate(14, isr14);
    idt_set_gate(15, isr15); idt_set_gate(16, isr16); idt_set_gate(17, isr17);
    idt_set_gate(18, isr18); idt_set_gate(19, isr19); idt_set_gate(20, isr20);
    idt_set_gate(21, isr21); idt_set_gate(22, isr22); idt_set_gate(23, isr23);
    idt_set_gate(24, isr24); idt_set_gate(25, isr25); idt_set_gate(26, isr26);
    idt_set_gate(27, isr27); idt_set_gate(28, isr28); idt_set_gate(29, isr29);
    idt_set_gate(30, isr30); idt_set_gate(31, isr31);

    /* Hardware IRQs */
    idt_set_gate(0x20, irq0);  idt_set_gate(0x21, irq1);
    idt_set_gate(0x22, irq2);  idt_set_gate(0x23, irq3);
    idt_set_gate(0x24, irq4);  idt_set_gate(0x25, irq5);
    idt_set_gate(0x26, irq6);  idt_set_gate(0x27, irq7);
    idt_set_gate(0x28, irq8);  idt_set_gate(0x29, irq9);
    idt_set_gate(0x2A, irq10); idt_set_gate(0x2B, irq11);
    idt_set_gate(0x2C, irq12); idt_set_gate(0x2D, irq13);
    idt_set_gate(0x2E, irq14); idt_set_gate(0x2F, irq15);

    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}
