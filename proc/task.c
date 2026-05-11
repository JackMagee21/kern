#include "task.h"
#include "heap.h"
#include "vmm.h"
#include "timer.h"
#include <stdint.h>
#include <stddef.h>

/* Defined in cpu/context_switch.s */
extern void switch_context(uint32_t *old_esp_ptr, uint32_t new_esp);

static task_t  *current   = NULL;
static task_t  *task_list = NULL;
static uint32_t next_pid  = 1;  /* 0 reserved for the boot/kernel task */

/* ── Internal helpers ─────────────────────────────────────────────────── */

static void list_append(task_t *t) {
    if (!task_list) {
        task_list = t;
        t->next   = t;
        return;
    }
    task_t *tail = task_list;
    while (tail->next != task_list) tail = tail->next;
    tail->next = t;
    t->next    = task_list;
}

/* ── Public API ───────────────────────────────────────────────────────── */

void task_set_initial(task_t *t) {
    current   = t;
    task_list = t;
    t->next   = t;
}

task_t *task_current(void)   { return current;   }
task_t *task_list_head(void) { return task_list;  }

task_t *task_create(const char *name, void (*fn)(void)) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return NULL;

    /* Zero the struct. */
    for (size_t i = 0; i < sizeof(task_t); i++) ((uint8_t *)t)[i] = 0;

    /* Copy name (bounded). */
    for (size_t i = 0; i < TASK_NAME_LEN - 1 && name[i]; i++)
        t->name[i] = name[i];

    t->pid   = next_pid++;
    t->cr3   = vmm_get_cr3();
    t->state = TASK_READY;

    /*
     * Build the initial stack frame that switch_context will restore.
     * Layout from bottom (highest address) to top of frame:
     *   fn       ← popped by ret as the return address into the task function
     *   0x200    ← eflags: IF=1 so the new task runs with interrupts enabled
     *   0        ← ebp
     *   0        ← edi
     *   0        ← esi
     *   0        ← ebx   ← esp points here after set-up
     */
    uint32_t *sp = (uint32_t *)(t->stack + TASK_STACK_SIZE);
    *--sp = (uint32_t)fn;  /* return address → task entry point */
    *--sp = 0x200u;        /* eflags */
    *--sp = 0;             /* ebp */
    *--sp = 0;             /* edi */
    *--sp = 0;             /* esi */
    *--sp = 0;             /* ebx */
    t->esp = (uint32_t)sp;

    list_append(t);
    return t;
}

void task_yield(void) {
    if (!current) return;

    /* Find the next task that is READY (or RUNNING, but there should be
     * at most one RUNNING task — the current one). */
    task_t *next = current->next;
    while (next != current) {
        if (next->state == TASK_READY) break;
        next = next->next;
    }

    if (next == current) {
        /* Only one task exists, or all others are blocked.
         * If we are sleeping, hlt until the timer wakes us. */
        while (current->state == TASK_SLEEPING)
            __asm__ volatile ("hlt");
        return;
    }

    if (current->state == TASK_RUNNING)
        current->state = TASK_READY;

    task_t *old = current;
    current     = next;
    current->state = TASK_RUNNING;

    switch_context(&old->esp, current->esp);
}

void task_sleep(uint32_t ms) {
    if (!current) return;
    current->sleep_until = timer_get_ticks() + ms;
    current->state       = TASK_SLEEPING;
    task_yield();
}

__attribute__((noreturn)) void task_exit(void) {
    if (current) current->state = TASK_DEAD;
    task_yield();
    /* Should never reach here — spin if somehow no other task runs. */
    for (;;) __asm__ volatile ("hlt");
    __builtin_unreachable();
}
