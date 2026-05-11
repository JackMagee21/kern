#include "pic.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20   /* End-of-interrupt command */

#define ICW1_INIT 0x10   /* initialisation required  */
#define ICW1_ICW4 0x01   /* ICW4 will be sent        */
#define ICW4_8086 0x01   /* 8086/88 mode             */

void pic_init(void) {
    /* Save masks so we can restore them after remapping. */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* Start initialisation sequence (ICW1). */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2: vector offsets. */
    outb(PIC1_DATA, PIC_IRQ_BASE);      io_wait(); /* IRQ 0-7  → 0x20-0x27 */
    outb(PIC2_DATA, PIC_IRQ_BASE + 8);  io_wait(); /* IRQ 8-15 → 0x28-0x2F */

    /* ICW3: cascade wiring. */
    outb(PIC1_DATA, 0x04); io_wait(); /* master: slave on IRQ 2 (bit mask) */
    outb(PIC2_DATA, 0x02); io_wait(); /* slave:  cascade identity = 2       */

    /* ICW4: 8086 mode. */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Restore saved masks. */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI); /* slave must be acknowledged first */
    outb(PIC1_CMD, PIC_EOI);
}
