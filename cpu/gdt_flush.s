/*
 * gdt_flush — load the GDTR and reload all segment registers.
 *
 * C prototype: void gdt_flush(gdt_ptr_t *ptr);
 *
 * We can't reload CS with a normal mov, so we use a far jump (ljmp) to
 * atomically set CS = 0x08 (kernel code selector) and continue execution.
 * All data segment registers are then set to 0x10 (kernel data selector).
 */
.section .text
.global gdt_flush
.type gdt_flush, @function
gdt_flush:
    mov 4(%esp), %eax       /* first argument: pointer to gdt_ptr_t */
    lgdt (%eax)             /* load the new GDT                      */

    ljmp $0x08, $.reload_cs /* far jump → flushes the instruction pipeline
                             * and reloads CS with the code selector  */
.reload_cs:
    mov $0x10, %ax          /* kernel data selector                  */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ret

.size gdt_flush, . - gdt_flush
