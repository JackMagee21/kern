#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>
#include "idt.h"
#include "fd.h"
#include "signal.h"

#define TASK_STACK_SIZE  8192u
#define TASK_NAME_LEN    16u
#define TASK_MAX_FDS     8u

typedef enum {
    TASK_RUNNING  = 0,
    TASK_READY    = 1,
    TASK_SLEEPING = 2,
    TASK_DEAD     = 3,
    TASK_STOPPED  = 4,   /* paused by SIGTSTP / SIGSTOP; resumed by SIGCONT */
} task_state_t;

typedef struct task {
    uint32_t        esp;
    uint32_t        cr3;            /* physical address of page directory   */
    task_state_t    state;
    uint32_t        sleep_until;
    uint32_t        pid;
    char            name[TASK_NAME_LEN];
    /* User-mode fields — set by task_exec, used by elf_task_trampoline. */
    uint32_t        user_entry;     /* ELF entry point (user virtual)       */
    uint32_t        user_stack_top; /* initial user ESP (after argv setup)  */
    uint32_t        brk;            /* program break (end of user heap)     */
    /* Exit code stored by SYS_EXIT; returned by task_waitpid. */
    int             exit_code;
    /* Signal state. */
    uint32_t        pending_sigs;
    uint32_t        sig_action[NSIG];   /* SIG_DFL or SIG_IGN per signal */
    /* Open file descriptor table (fd 0/1 default to keyboard/VGA if NONE). */
    fd_t            fds[TASK_MAX_FDS];
    struct task    *next;
    /* Saved user-mode register state for fork trampoline. */
    registers_t     fork_regs;
    uint8_t         stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
} task_t;

/* Wrap the boot context as PID 0 (called by scheduler_init). */
void task_set_initial(task_t *t);

task_t *task_current(void);
task_t *task_list_head(void);

/*
 * Create a kernel-mode task.
 * Allocates a private page directory (vmm_create_pd) and an 8 KB kernel stack.
 */
task_t *task_create(const char *name, void (*fn)(void));

/*
 * Load and execute an ELF32 binary as a new user-mode task.
 * elf_data: pointer to raw ELF bytes in kernel memory.
 * elf_size: byte length (informational; bounds-checking is in elf_load).
 * Returns the new task_t, or NULL on failure.
 */
task_t *task_exec(const char *name, const void *elf_data, uint32_t elf_size);

void  task_yield(void);
void  task_sleep(uint32_t ms);
void  task_wait(task_t *t);         /* block until t reaches TASK_DEAD */
int32_t task_waitpid(uint32_t pid); /* block until pid dies; frees it; returns pid or -1 */

/*
 * Clone the current user task for fork().
 * Copies the page directory (vmm_clone_pd), duplicates the task_t, sets up
 * the child to resume at the fork point with eax=0.
 * Returns the child task_t, or NULL on failure.
 */
task_t *task_fork(registers_t *regs);

__attribute__((noreturn)) void task_exit(void);

/* ── fd helpers (used by syscall.c) ─────────────────────────────────────── */

/* Duplicate an fd entry (increments pipe refcounts, copies vfs_file_t). */
fd_t task_fd_dup(fd_t src);

/* Close an fd entry and reset it to FD_NONE. */
void task_fd_close(fd_t *fd);

/* Signal support. */
void     task_signal(task_t *t, int sig);
void     signals_deliver(void);     /* call at syscall/IRQ exit for user tasks */

/* Foreground task tracking (for Ctrl+C). */
void     task_set_fg(task_t *t);
task_t  *task_get_fg(void);

#endif
