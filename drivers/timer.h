#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

typedef void (*timer_tick_cb_t)(void);

void     timer_init(uint32_t hz);
uint32_t timer_get_ticks(void);

/* Register a callback invoked on every PIT tick (after the tick counter
 * is incremented).  Pass NULL to clear.  Only one callback is supported. */
void timer_set_tick_callback(timer_tick_cb_t cb);

#endif
