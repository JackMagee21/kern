#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* After remapping:
 *   IRQ 0-7  → vectors 0x20-0x27
 *   IRQ 8-15 → vectors 0x28-0x2F
 */
#define PIC_IRQ_BASE 0x20

void pic_init(void);
void pic_send_eoi(uint8_t irq);

#endif
