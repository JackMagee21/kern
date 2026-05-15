#include "task.h"
#include "signal.h"
#include "pipe.h"
#include "vfs.h"
#include "heap.h"
#include "vmm.h"
#include "timer.h"
#include "elf.h"
#include "usermode.h"
#include "tss.h"
#include <stdint.h>
#include <stddef.h>

/* ── fd helpers ───────────────────────────────────────────────────────────── */

fd_t task_fd_dup(fd_t src) {
    switch (src.type) {
        case FD_FILE:
        case FD_FILE_W: {
            /* Give the child its own vfs_file_t so positions are independent. */
            vfs_file_t *f2 = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
            if (f2) *f2 = *src.file;
            fd_t d; d.type = src.type; d.file = f2;
            return d;
        }
        case FD_PIPE_R: src.pipe->readers++; return src;
        case FD_PIPE_W: src.pipe->writers++; return src;
        default:        return src; /* FD_NONE */
    }
}

void task_fd_close(fd_t *fd) {
    switch (fd->type) {
        case FD_FILE:
        case FD_FILE_W: vfs_close(fd->file);          break;
        case FD_PIPE_R: pipe_close_reader(fd->pipe);  break;
        case FD_PIPE_W: pipe_close_writer(fd->pipe);  break;
        default: break;
    }
    fd->type = FD_NONE;
    fd->pipe = (void *)0;
}

extern void switch_context(uint32_t *old_esp_ptr, uint32_t new_esp);
extern void fork_enter_user(registers_t *r);

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
     * switch_context restores in this order: popf, pop ebp/edi/esi/ebx, ret.
     * So the stack grows down: fn is at the top, eflags is at the bottom
     * (lowest address, pointed to by t->esp, popped first by popf).
     */
    uint32_t *sp = (uint32_t *)(t->stack + TASK_STACK_SIZE);
    *--sp = (uint32_t)fn;   /* return address (popped last, by ret)  */
    *--sp = 0;               /* ebx                                   */
    *--sp = 0;               /* esi                                   */
    *--sp = 0;               /* edi                                   */
    *--sp = 0;               /* ebp                                   */
    *--sp = 0x200u;          /* eflags: IF=1 (popped first, by popf) */
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

    uint32_t brk = 0;
    uint32_t entry = elf_load(elf_data, pd, &brk);
    if (!entry) { vmm_destroy_pd(pd); return NULL; }

    task_t *t = task_alloc(name, elf_task_trampoline, pd);
    if (!t) { vmm_destroy_pd(pd); return NULL; }

    t->user_entry     = entry;
    t->user_stack_top = elf_setup_argv(pd, name); /* argv[0] = program name */
    t->brk            = brk;
    return t;
}

/* ── Scheduler primitives ─────────────────────────────────────────────── */

void task_yield(void) {
    if (!current) return;

    /*
     * Save IF and disable interrupts for the entire critical section.
     * If task_yield is called directly (IF=1) the timer could otherwise fire
     * after "current = next" but before switch_context, causing scheduler_tick
     * to read the wrong current and corrupt saved stack pointers.
     *
     * The eflags local lives on the calling task's kernel stack.  When
     * switch_context saves this task's ESP and later restores it, eflags still
     * holds the pre-CLI value for *this* task, so the conditional sti below
     * correctly restores IF on a per-task basis.
     */
    uint32_t eflags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags));

    task_t *next = current->next;
    while (next != current) {
        if (next->state == TASK_READY) break;
        next = next->next;
    }

    if (next == current) {
        /* No ready task — spin on hlt until a sleeping task is woken.
         * Re-enable interrupts first so the timer IRQ can fire. */
        __asm__ volatile ("sti");
        while (current->state == TASK_SLEEPING)
            __asm__ volatile ("hlt");
        return;
    }

    if (current->state == TASK_RUNNING) current->state = TASK_READY;

    task_t *old = current;
    current      = next;
    current->state = TASK_RUNNING;

    if (old->cr3 != current->cr3)
        vmm_switch(current->cr3);

    switch_context(&old->esp, current->esp);

    /* Now executing as the resumed task. */
    tss_set_kernel_stack((uint32_t)current->stack + TASK_STACK_SIZE);

    /* Restore IF to whatever it was when this task last entered task_yield. */
    if (eflags & 0x200)
        __asm__ volatile ("sti");
}

void task_sleep(uint32_t ms) {
    if (!current) return;
    current->sleep_until = timer_get_ticks() + ms;
    current->state       = TASK_SLEEPING;
    task_yield();
}

void task_wait(task_t *t) {
    while (t && t->state != TASK_DEAD)
        task_yield();
}

int32_t task_waitpid(uint32_t pid) {
    /* Find the task with the requested pid. */
    task_t *head = task_list;
    if (!head) return -1;
    task_t *t = head;
    do {
        if (t->pid == pid) break;
        t = t->next;
    } while (t != head);
    if (t->pid != pid) return -1;

    /* Yield until it dies or stops. */
    while (t->state != TASK_DEAD && t->state != TASK_STOPPED)
        task_yield();

    if (t->state == TASK_STOPPED)
        return WAIT_STOPPED;   /* caller can send SIGCONT and call again */

    /* TASK_DEAD — harvest exit code and free. */
    int32_t code = t->exit_code;

    task_t *prev = t;
    while (prev->next != t) prev = prev->next;
    if (prev == t) {
        task_list = NULL;
    } else {
        prev->next = t->next;
        if (task_list == t) task_list = t->next;
    }

    kfree(t);
    return code;
}

/* ── Fork trampoline ──────────────────────────────────────────────────── */

static void fork_trampoline(void) {
    task_t *t = task_current();
    tss_set_kernel_stack((uint32_t)t->stack + TASK_STACK_SIZE);
    vmm_switch(t->cr3);
    fork_enter_user(&t->fork_regs);
    __builtin_unreachable();
}

task_t *task_fork(registers_t *regs) {
    task_t *parent = task_current();

    uint32_t child_pd = vmm_clone_pd(parent->cr3);
    if (!child_pd) return NULL;

    task_t *child = task_alloc(parent->name, fork_trampoline, child_pd);
    if (!child) { vmm_destroy_pd(child_pd); return NULL; }

    child->fork_regs     = *regs;
    child->fork_regs.eax = 0;          /* fork() returns 0 in the child */
    child->brk           = parent->brk;
    child->user_entry    = parent->user_entry;
    child->user_stack_top = parent->user_stack_top;
    for (size_t i = 0; i < TASK_MAX_FDS; i++)
        child->fds[i] = task_fd_dup(parent->fds[i]);

    return child;
}

__attribute__((noreturn)) void task_exit(void) {
    if (current) {
        /* Close all open file descriptors (decrements pipe refcounts etc.). */
        for (size_t i = 0; i < TASK_MAX_FDS; i++)
            task_fd_close(&current->fds[i]);

        /* Switch to kernel PD so we can safely free the user address space. */
        uint32_t kpd = vmm_get_phys_pd();
        if (current->cr3 != kpd) {
            vmm_switch(kpd);
            vmm_destroy_pd(current->cr3);
            current->cr3 = kpd;
        }
        current->state = TASK_DEAD;
    }
    task_yield();
    for (;;) __asm__ volatile ("hlt");
    __builtin_unreachable();
}

/* ── Signal support ───────────────────────────────────────────────────────── */

static task_t *fg_task = NULL;

void task_set_fg(task_t *t)  { fg_task = t; }
task_t *task_get_fg(void)    { return fg_task; }

void task_signal(task_t *t, int sig) {
    if (!t || sig < 1 || sig >= NSIG) return;
    /* SIGCONT: resume immediately regardless of disposition. */
    if (sig == SIGCONT) {
        if (t->state == TASK_STOPPED)
            t->state = TASK_READY;
        return;
    }
    if (t->sig_action[sig] == SIG_IGN) return;
    t->pending_sigs |= (1u << sig);
    /* Wake a sleeping task so it sees the signal promptly. */
    if (t->state == TASK_SLEEPING)
        t->state = TASK_READY;
}

/*
 * Deliver any pending signals for the current (user) task.
 * Must be called just before returning to ring-3 (end of syscall/IRQ).
 * For now all terminating signals use the default handler (task_exit).
 */
void signals_deliver(void) {
    task_t *t = current;
    if (!t || !t->pending_sigs || !t->user_entry) return;
    uint32_t p = t->pending_sigs;

    /* SIGTSTP / SIGSTOP: stop the task and yield; resume when SIGCONT arrives. */
    if (p & ((1u << SIGTSTP) | (1u << SIGSTOP))) {
        t->pending_sigs &= ~((1u << SIGTSTP) | (1u << SIGSTOP));
        t->state = TASK_STOPPED;
        task_yield();   /* returns here after SIGCONT sets state=TASK_READY */
        return;
    }

    t->pending_sigs = 0;
    if (p & ((1u << SIGINT) | (1u << SIGKILL) | (1u << SIGPIPE)))
        task_exit();
}
