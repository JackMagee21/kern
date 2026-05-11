#ifndef TSS_H
#define TSS_H

#include <stdint.h>
#include <stddef.h>

/*
 * 32-bit Task State Segment.
 * The CPU reads ESP0/SS0 from here whenever an interrupt or exception
 * transitions from ring 3 → ring 0.  All other fields are unused by us
 * but must be present in the struct so the TSS descriptor has the right size.
 */
typedef struct __attribute__((packed)) {
    uint32_t prev;
    uint32_t esp0;        /* kernel stack pointer for ring-3 → ring-0 */
    uint32_t ss0;         /* kernel stack segment  (GDT_KERNEL_DATA)  */
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3;
    uint32_t eip, eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;  /* set to sizeof(tss_t) → no I/O permission bitmap */
} tss_t;

/* Initialise the TSS and write kernel SS0.  Called from gdt_init(). */
void tss_init(uint32_t kernel_ss, uint32_t kernel_esp);

/* Load the TR register with the TSS selector (0x28).
 * Must be called after gdt_flush() has loaded the new GDT. */
void tss_flush(void);

/* Update the kernel stack pointer stored in the TSS.
 * Call this before returning to user mode so ring-3→ring-0 transitions
 * use the correct per-task kernel stack. */
void tss_set_kernel_stack(uint32_t esp0);

/* Return a pointer to the static TSS instance (used by gdt_init). */
tss_t *tss_get(void);

#endif
