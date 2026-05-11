#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

#define TASK_STACK_SIZE  8192u  /* 8 KB kernel stack per task */
#define TASK_NAME_LEN    16u

typedef enum {
    TASK_RUNNING  = 0,
    TASK_READY    = 1,
    TASK_SLEEPING = 2,
    TASK_DEAD     = 3,
} task_state_t;

typedef struct task {
    uint32_t       esp;          /* saved kernel stack pointer               */
    uint32_t       cr3;          /* page directory (shared among all kernel tasks) */
    task_state_t   state;
    uint32_t       sleep_until;  /* tick value at which to wake (SLEEPING only) */
    uint32_t       pid;
    char           name[TASK_NAME_LEN];
    struct task   *next;         /* circular linked list                     */
    /* Per-task kernel stack — must be last; 16-byte aligned for ABI safety. */
    uint8_t        stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
} task_t;

/*
 * Allocate a new task on the heap and add it to the ready queue.
 * fn will be called when the task is first scheduled.
 * Returns NULL if kmalloc fails.
 */
task_t *task_create(const char *name, void (*fn)(void));

/*
 * Set the initial task (the boot thread).  Called once by scheduler_init
 * before any other task is created; sets current and task_list.
 */
void task_set_initial(task_t *t);

/* Return the currently running task. */
task_t *task_current(void);

/* Return the head of the circular task list (for iteration). */
task_t *task_list_head(void);

/* Cooperatively yield the CPU to the next ready task. */
void task_yield(void);

/*
 * Sleep for ms milliseconds then yield.
 * If no other task is ready the CPU halts (hlt) until the timer wakes us.
 */
void task_sleep(uint32_t ms);

/* Mark the current task dead and yield.  Never returns. */
__attribute__((noreturn)) void task_exit(void);

#endif
