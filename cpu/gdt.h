#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* One GDT entry: 8 bytes describing a memory segment. */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit;
    uint8_t  base_high;
} gdt_entry_t;

/* Pointer handed to lgdt. */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

/*
 * GDT layout (byte offsets = selectors):
 *   0x00  null
 *   0x08  kernel code  ring 0  execute/read
 *   0x10  kernel data  ring 0  read/write
 *   0x18  user code    ring 3  execute/read   (RPL=3 → selector 0x1B)
 *   0x20  user data    ring 3  read/write     (RPL=3 → selector 0x23)
 *   0x28  TSS          system  available 32-bit TSS
 */
#define GDT_KERNEL_CODE  0x08u
#define GDT_KERNEL_DATA  0x10u
#define GDT_USER_CODE    0x1Bu  /* 0x18 | RPL=3 */
#define GDT_USER_DATA    0x23u  /* 0x20 | RPL=3 */
#define GDT_TSS_SEL      0x28u

void gdt_init(void);

#endif
