#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

#define TASK_STACK_SIZE  8192u
#define TASK_NAME_LEN    16u
#define TASK_MAX_FDS     8u

typedef enum {
    TASK_RUNNING  = 0,
    TASK_READY    = 1,
    TASK_SLEEPING = 2,
    TASK_DEAD     = 3,
} task_state_t;

/* Forward-declare vfs_file_t to avoid including vfs.h here. */
struct vfs_file;

typedef struct task {
    uint32_t        esp;
    uint32_t        cr3;            /* physical address of page directory   */
    task_state_t    state;
    uint32_t        sleep_until;
    uint32_t        pid;
    char            name[TASK_NAME_LEN];
    /* User-mode fields — set by task_exec, used by elf_task_trampoline. */
    uint32_t        user_entry;     /* ELF entry point (user virtual)       */
    uint32_t        user_stack_top; /* initial user stack pointer           */
    /* Open file descriptor table. */
    struct vfs_file *fds[TASK_MAX_FDS];
    struct task    *next;
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
__attribute__((noreturn)) void task_exit(void);

#endif
