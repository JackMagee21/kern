#include "gdt.h"
#include "tss.h"
#include <stdint.h>

#define GDT_ENTRIES 6

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

extern void gdt_flush(gdt_ptr_t *ptr);

static void gdt_set_entry(int i, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t flags)
{
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;

    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].flags_limit = (flags << 4) | ((limit >> 16) & 0x0F);

    gdt[i].access      = access;
}

void gdt_init(void) {
    /* Initialise TSS first so we can take its address for the descriptor. */
    tss_init(GDT_KERNEL_DATA, 0); /* esp0 = 0; updated per-task before ring-3 entry */

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    /* 0x00 — null */
    gdt_set_entry(0, 0, 0x00000, 0x00, 0x0);

    /* 0x08 — kernel code: base 0, limit 4 GB, ring 0, 32-bit, execute/read
     *   access 0x9A = present | DPL=0 | S=1 | type=1010 (code, exec, readable)
     *   flags  0xC  = granularity=1 (4 KB) | size=1 (32-bit)             */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC);

    /* 0x10 — kernel data: base 0, limit 4 GB, ring 0, 32-bit, read/write
     *   access 0x92 = present | DPL=0 | S=1 | type=0010 (data, rw)      */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC);

    /* 0x18 — user code: same as kernel code but DPL=3
     *   access 0xFA = present | DPL=3 | S=1 | type=1010                 */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xC);

    /* 0x20 — user data: same as kernel data but DPL=3
     *   access 0xF2 = present | DPL=3 | S=1 | type=0010                 */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC);

    /* 0x28 — TSS: system segment, type 0x9 = available 32-bit TSS
     *   access 0x89 = present | DPL=0 | S=0 | type=1001
     *   flags  0x0  = byte granularity (TSS fits in < 64 KB)             */
    tss_t   *t   = tss_get();
    uint32_t base  = (uint32_t)t;
    uint32_t limit = (uint32_t)(sizeof(tss_t) - 1);
    gdt_set_entry(5, base, limit, 0x89, 0x0);

    gdt_flush(&gdt_ptr);
    tss_flush();
}
