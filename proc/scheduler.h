#ifndef SCHEDULER_H
#define SCHEDULER_H

/*
 * Initialise the scheduler.
 * Wraps the current boot execution context as PID 0 ("kernel") and
 * registers scheduler_tick as the timer's per-tick callback.
 * Must be called after heap_init() and timer_init().
 */
void scheduler_init(void);

/*
 * Called from the PIT IRQ0 handler on every millisecond tick.
 * Walks the task list and wakes any task whose sleep_until has passed.
 * Does NOT perform a context switch — tasks yield cooperatively.
 */
void scheduler_tick(void);

/* Print a one-line summary of every task via kprintf. */
void scheduler_print_tasks(void);

#endif
