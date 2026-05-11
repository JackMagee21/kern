/*
 * ISR / IRQ entry stubs.
 *
 * The CPU pushes SS, ESP, EFLAGS, CS, EIP automatically on an interrupt.
 * For exceptions that carry an error code the CPU also pushes that.
 * We push a dummy 0 for exceptions that don't, so the stack frame is always
 * the same shape (registers_t in idt.h).
 *
 * Each stub:
 *   1. Pushes int_no (and err_code if the CPU didn't).
 *   2. Jumps to the shared isr_common stub.
 *
 * isr_common:
 *   3. Saves all GP registers (pusha) and DS.
 *   4. Switches DS to the kernel data segment.
 *   5. Calls the C handler: isr_dispatch(registers_t *).
 *   6. Restores everything and returns with iret.
 */

.macro ISR_NOERR num
.global isr\num
isr\num:
    push $0          /* dummy error code */
    push $\num
    jmp isr_common
.endm

.macro ISR_ERR num
.global isr\num
isr\num:
    push $\num       /* error code already on stack from CPU */
    jmp isr_common
.endm

.macro IRQ num vec
.global irq\num
irq\num:
    push $0
    push $\vec
    jmp isr_common
.endm

/* CPU exceptions 0-31 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

/* Hardware IRQs 0-15 mapped to vectors 0x20-0x2F */
IRQ  0, 0x20
IRQ  1, 0x21
IRQ  2, 0x22
IRQ  3, 0x23
IRQ  4, 0x24
IRQ  5, 0x25
IRQ  6, 0x26
IRQ  7, 0x27
IRQ  8, 0x28
IRQ  9, 0x29
IRQ 10, 0x2A
IRQ 11, 0x2B
IRQ 12, 0x2C
IRQ 13, 0x2D
IRQ 14, 0x2E
IRQ 15, 0x2F

/* Shared handler — called by every stub above. */
.extern isr_dispatch

isr_common:
    pusha                   /* push eax,ecx,edx,ebx,esp,ebp,esi,edi */

    mov %ds, %ax
    push %eax               /* save DS */

    mov $0x10, %ax          /* load kernel data segment */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp               /* registers_t* argument for isr_dispatch */
    call isr_dispatch
    add $4, %esp            /* pop the argument */

    pop %eax                /* restore DS */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp            /* discard err_code and int_no */
    iret
