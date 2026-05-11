#include "tss.h"
#include <stdint.h>
#include <stddef.h>

static tss_t tss;

tss_t *tss_get(void) { return &tss; }

void tss_init(uint32_t kernel_ss, uint32_t kernel_esp) {
    /* Zero the entire TSS. */
    uint8_t *p = (uint8_t *)&tss;
    for (size_t i = 0; i < sizeof(tss); i++) p[i] = 0;

    tss.ss0         = kernel_ss;
    tss.esp0        = kernel_esp;
    /* Point past the struct → no I/O permission bitmap entries. */
    tss.iomap_base  = (uint16_t)sizeof(tss_t);
}

void tss_flush(void) {
    /* TSS selector = 0x28 (GDT entry 5, byte offset 5×8 = 40). */
    __asm__ volatile ("ltr %0" :: "rm"((uint16_t)0x28));
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}
