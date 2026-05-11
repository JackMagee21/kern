#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* One IDT entry: 8 bytes describing an interrupt gate. */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;  /* bits  0-15  of handler address */
    uint16_t selector;    /* code segment selector (0x08)   */
    uint8_t  zero;        /* always 0                       */
    uint8_t  type_attr;   /* gate type, DPL, present bit    */
    uint16_t offset_high; /* bits 16-31  of handler address */
} idt_entry_t;

/* Pointer handed to lidt. */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

/* Registers pushed by our ISR stubs, in order from last push to first. */
typedef struct __attribute__((packed)) {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;            /* pushed by CPU */
} registers_t;

typedef void (*isr_handler_t)(registers_t *);

void idt_init(void);
void idt_register_handler(uint8_t vector, isr_handler_t handler);

#endif
