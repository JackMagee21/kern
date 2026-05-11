/* Multiboot header constants */
.set ALIGN,    1<<0              /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1              /* provide memory map */
.set FLAGS,    ALIGN | MEMINFO   /* multiboot flag field */
.set MAGIC,    0x1BADB002        /* magic number GRUB looks for */
.set CHECKSUM, -(MAGIC + FLAGS)  /* checksum to prove we are multiboot */

/* 
 * Multiboot header — must be in the first 8KB of the kernel binary.
 * GRUB scans for this to identify your kernel.
 */
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

/*
 * Reserve 16KB for the initial kernel stack.
 * The .bss section is zero-initialised and not stored in the binary.
 */
.section .bss
.align 16
stack_bottom:
.skip 16384          /* 16 KiB */
stack_top:

/*
 * Kernel entry point — GRUB jumps here.
 * We are now in 32-bit protected mode with no paging.
 */
.section .text
.global _start
.type _start, @function
_start:
    /* Set up the stack pointer */
    mov $stack_top, %esp

    /* Call the C kernel — this should never return */
    call kernel_main

    /*
     * If kernel_main somehow returns, hang the CPU:
     * disable interrupts and halt forever.
     */
.hang:
    cli
    hlt
    jmp .hang

.size _start, . - _start