#include "task.h"
#include "heap.h"
#include "vmm.h"
#include "timer.h"
#include "elf.h"
#include "usermode.h"
#include <stdint.h>
#include <stddef.h>

extern void switch_context(uint32_t *old_esp_ptr, uint32_t new_esp);

static task_t  *current   = NULL;
static task_t  *task_list = NULL;
static uint32_t next_pid  = 1;

/* ── List helpers ─────────────────────────────────────────────────────── */

static void list_append(task_t *t) {
    if (!task_list) { task_list = t; t->next = t; return; }
    task_t *tail = task_list;
    while (tail->next != task_list) tail = tail->next;
    tail->next = t;
    t->next    = task_list;
}

/* ── Common task initialisation ───────────────────────────────────────── */

static task_t *task_alloc(const char *name, void (*fn)(void), uint32_t pd_phys) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return NULL;

    for (size_t i = 0; i < sizeof(task_t); i++) ((uint8_t *)t)[i] = 0;
    for (size_t i = 0; i < TASK_NAME_LEN - 1 && name[i]; i++) t->name[i] = name[i];

    t->pid   = next_pid++;
    t->cr3   = pd_phys;
    t->state = TASK_READY;

    /*
     * Build the initial switch_context frame on the task's kernel stack.
     * switch_context restores: popf, pop ebp/edi/esi/ebx, ret → fn.
     */
    uint32_t *sp = (uint32_t *)(t->stack + TASK_STACK_SIZE);
    *--sp = (uint32_t)fn;   /* return address  */
    *--sp = 0x200u;          /* eflags: IF=1    */
    *--sp = 0;               /* ebp             */
    *--sp = 0;               /* edi             */
    *--sp = 0;               /* esi             */
    *--sp = 0;               /* ebx             */
    t->esp = (uint32_t)sp;

    list_append(t);
    return t;
}

/* ── Trampoline for ELF tasks ─────────────────────────────────────────── */

static void elf_task_trampoline(void) {
    task_t *t = task_current();
    enter_usermode((void (*)(void))t->user_entry, t->user_stack_top);
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
    uint32_t pd = vmm_create_pd();
    if (!pd) return NULL;
    return task_alloc(name, fn, pd);
}

task_t *task_exec(const char *name, const void *elf_data, uint32_t elf_size) {
    (void)elf_size;

    uint32_t pd = vmm_create_pd();
    if (!pd) return NULL;

    uint32_t entry = elf_load(elf_data, pd);
    if (!entry) { vmm_destroy_pd(pd); return NULL; }

    task_t *t = task_alloc(name, elf_task_trampoline, pd);
    if (!t) { vmm_destroy_pd(pd); return NULL; }

    t->user_entry     = entry;
    t->user_stack_top = USER_STACK_TOP;
    return t;
}

/* ── Scheduler primitives ─────────────────────────────────────────────── */

void task_yield(void) {
    if (!current) return;

    task_t *next = current->next;
    while (next != current) {
        if (next->state == TASK_READY) break;
        next = next->next;
    }

    if (next == current) {
        while (current->state == TASK_SLEEPING)
            __asm__ volatile ("hlt");
        return;
    }

    if (current->state == TASK_RUNNING) current->state = TASK_READY;

    task_t *old = current;
    current      = next;
    current->state = TASK_RUNNING;

    /* Reload CR3 if the new task has a different address space. */
    if (old->cr3 != current->cr3)
        vmm_switch(current->cr3);

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
    for (;;) __asm__ volatile ("hlt");
    __builtin_unreachable();
}
