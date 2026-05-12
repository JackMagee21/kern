#include "scheduler.h"
#include "task.h"
#include "timer.h"
#include "heap.h"
#include "vmm.h"
#include "printf.h"
#include <stdint.h>
#include <stddef.h>

/* Time-slice length in milliseconds (timer fires at 1 kHz). */
#define SCHED_QUANTUM_MS 10u

static uint32_t sched_quantum = 0;

static const char *state_name(task_state_t s) {
    switch (s) {
        case TASK_RUNNING:  return "running ";
        case TASK_READY:    return "ready   ";
        case TASK_SLEEPING: return "sleeping";
        case TASK_DEAD:     return "dead    ";
        default:            return "unknown ";
    }
}

void scheduler_init(void) {
    /* Allocate a task_t to represent the current boot context (PID 0). */
    task_t *boot = (task_t *)kmalloc(sizeof(task_t));
    if (!boot) return; /* should never fail this early */

    for (size_t i = 0; i < sizeof(task_t); i++) ((uint8_t *)boot)[i] = 0;

    /* "kernel" is the shell / boot thread. */
    const char *n = "kernel";
    for (size_t i = 0; i < TASK_NAME_LEN - 1 && n[i]; i++) boot->name[i] = n[i];

    boot->pid   = 0;
    boot->cr3   = vmm_get_phys_pd();
    boot->state = TASK_RUNNING;
    /* boot->esp is zero — it will be written by switch_context on first yield. */

    task_set_initial(boot);

    /* Hook the timer so sleeping tasks are woken each millisecond. */
    timer_set_tick_callback(scheduler_tick);
}

void scheduler_tick(void) {
    task_t *head = task_list_head();
    if (!head) return;

    uint32_t now = timer_get_ticks();
    task_t  *t   = head;

    /* Wake any tasks whose sleep period has expired. */
    do {
        if (t->state == TASK_SLEEPING && t->sleep_until <= now)
            t->state = TASK_READY;
        t = t->next;
    } while (t != head);

    /* Preempt the current task once per quantum if another task is ready.
     * We check explicitly before calling task_yield so we never enter the
     * hlt spin loop inside task_yield while interrupts are disabled. */
    if (++sched_quantum < SCHED_QUANTUM_MS)
        return;
    sched_quantum = 0;

    t = task_current()->next;
    while (t != task_current()) {
        if (t->state == TASK_READY) {
            task_yield();
            return;
        }
        t = t->next;
    }
}

void scheduler_print_tasks(void) {
    task_t *head = task_list_head();
    if (!head) { kprintf("  (no tasks)\n"); return; }

    kprintf("  PID  STATE     NAME\n");
    kprintf("  ---  --------  ----------------\n");

    task_t *t = head;
    do {
        kprintf("  %u\t%s  %s\n", t->pid, state_name(t->state), t->name);
        t = t->next;
    } while (t != head);
}
