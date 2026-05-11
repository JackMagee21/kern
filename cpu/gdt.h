#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* One GDT entry: 8 bytes describing a memory segment. */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;   /* bits  0-15  of limit  */
    uint16_t base_low;    /* bits  0-15  of base   */
    uint8_t  base_mid;    /* bits 16-23  of base   */
    uint8_t  access;      /* present, DPL, type    */
    uint8_t  flags_limit; /* high 4 bits = flags, low 4 = limit[19:16] */
    uint8_t  base_high;   /* bits 24-31  of base   */
} gdt_entry_t;

/* Pointer handed to lgdt: 6 bytes (limit + base address). */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

void gdt_init(void);

#endif
