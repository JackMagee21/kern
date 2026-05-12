/* ── Multiboot 1 header + boot trampoline ───────────────────────────────────
 * This section lives at VMA = LMA = 0x100000.  GRUB jumps here before paging.
 * FLAGS bit 0 = align modules, bit 1 = request memory map.
 */
.set MB_ALIGN,    1 << 0
.set MB_MEMINFO,  1 << 1
.set MB_FLAGS,    MB_ALIGN | MB_MEMINFO
.set MB_MAGIC,    0x1BADB002
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.set KERNEL_VMA,  0xC0000000

/* ── BSS: boot page directory + kernel stack ───────────────────────────── */
.section .bss
.align 4096
boot_page_dir:
    .skip 4096          /* 1024 × 4-byte PDEs, zeroed by GRUB */

.align 16
stack_bottom:
    .skip 16384         /* 16 KiB kernel stack */
stack_top:

/* ── Boot section: multiboot header + _start (physical address) ─────────── */
.section .boot, "ax"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.global _start
.type _start, @function
_start:
    /*
     * GRUB leaves us in 32-bit protected mode, no paging.
     * eax = Multiboot magic, ebx = physical address of mbi.
     * All symbol references resolve to VMA (0xC01xxxxx);
     * subtract KERNEL_VMA to get physical.
     *
     * Save registers — we have no stack yet.
     */
    mov %eax, %esi      /* magic    */
    mov %ebx, %edi      /* mbi_phys */

    /*
     * Fill in the boot page directory (already zeroed by GRUB).
     * 4 MB PSE pages for a quick flat mapping:
     *
     *   PDE[  0] = 0x00000000 → identity-map first 4 MB
     *   PDE[768] = 0xC0000000 → physical 0x000000  (kernel text/data)
     *   PDE[769] = 0xC0400000 → physical 0x400000  (second 4 MB)
     */
    mov $(boot_page_dir - KERNEL_VMA), %eax
    movl $0x00000083, (%eax)            /* PDE[0]:   identity, PSE, P+W */
    movl $0x00000083, (768*4)(%eax)     /* PDE[768]: 0xC0000000→0x0     */
    movl $0x00400083, (769*4)(%eax)     /* PDE[769]: 0xC0400000→0x400000*/

    /* Enable CR4.PSE */
    mov %cr4, %eax
    or  $0x10, %eax
    mov %eax, %cr4

    /* Load CR3 with the physical address of the boot page directory */
    mov $(boot_page_dir - KERNEL_VMA), %eax
    mov %eax, %cr3

    /* Enable paging (CR0.PG) */
    mov %cr0, %eax
    or  $0x80000000, %eax
    mov %eax, %cr0

    /*
     * Still executing via identity map (PDE[0]).
     * Jump to the higher-half virtual address.
     */
    lea _start_higher, %eax
    jmp *%eax
.size _start, . - _start

/* ── Higher-half entry: running at 0xC01xxxxx ──────────────────────────── */
.section .text
.global _start_higher
.type _start_higher, @function
_start_higher:
    /*
     * We are now at virtual 0xC01xxxxx.
     * The identity map (PDE[0]) is still active; vmm_init() removes it.
     */
    mov $stack_top, %esp

    push %edi   /* arg 2: mbi_phys */
    push %esi   /* arg 1: Multiboot magic */
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
.size _start_higher, . - _start_higher
