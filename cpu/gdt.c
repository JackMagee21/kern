#include "gdt.h"

/* Segment selectors (byte offset into the GDT):
 *   0x00 — null
 *   0x08 — kernel code  (ring 0, execute/read)
 *   0x10 — kernel data  (ring 0, read/write)
 */
#define GDT_ENTRIES 3

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

/* Defined in gdt_flush.s — loads the GDTR and reloads all segment registers. */
extern void gdt_flush(gdt_ptr_t *ptr);

static void gdt_set_entry(int i, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t flags)
{
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;

    gdt[i].limit_low   = limit & 0xFFFF;
    /* flags occupy the high nibble; limit[19:16] the low nibble */
    gdt[i].flags_limit = (flags << 4) | ((limit >> 16) & 0x0F);

    gdt[i].access      = access;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    /* Entry 0: required null descriptor */
    gdt_set_entry(0, 0, 0x00000, 0x00, 0x0);

    /* Entry 1: kernel code — base 0, limit 4 GB, ring 0, 32-bit, execute/read
     *   access 0x9A = 1001 1010
     *     present=1, DPL=00, S=1, type=1010 (code, exec, readable)
     *   flags  0xC  = 1100
     *     granularity=1 (4 KB pages), size=1 (32-bit)
     */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC);

    /* Entry 2: kernel data — base 0, limit 4 GB, ring 0, 32-bit, read/write
     *   access 0x92 = 1001 0010
     *     present=1, DPL=00, S=1, type=0010 (data, readable, writable)
     */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC);

    gdt_flush(&gdt_ptr);
}
