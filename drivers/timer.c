#include "timer.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
/* Channel 0, lobyte/hibyte access, mode 3 (square wave), binary */
#define PIT_MODE     0x36
#define PIT_BASE_HZ  1193182

static volatile uint32_t ticks = 0;
static timer_tick_cb_t   tick_cb = NULL;

void timer_set_tick_callback(timer_tick_cb_t cb) {
    tick_cb = cb;
}

static void timer_handler(registers_t *regs) {
    (void)regs;
    ticks++;
    if (tick_cb) tick_cb();
}

uint32_t timer_get_ticks(void) {
    return ticks;
}

void timer_init(uint32_t hz) {
    uint32_t divisor = PIT_BASE_HZ / hz;
    outb(PIT_CMD,      PIT_MODE);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

    /* IRQ 0 → vector 0x20 */
    idt_register_handler(PIC_IRQ_BASE, timer_handler);
}
