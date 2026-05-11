#include "usermode.h"
#include "tss.h"
#include "gdt.h"
#include "task.h"
#include <stdint.h>

__attribute__((noreturn))
void enter_usermode(void (*fn)(void), uint32_t user_stack_top) {
    task_t *cur = task_current();
    tss_set_kernel_stack((uint32_t)cur->stack + TASK_STACK_SIZE);

    /*
     * Build the iret frame that drops us to ring-3.
     * iret (with privilege change) pops: EIP, CS, EFLAGS, ESP, SS.
     *
     * We first switch the data segment registers to the user selector so
     * that ring-3 code starts with the correct DS.  All four values are
     * pre-loaded into general registers by the "r" constraints before any
     * assembly executes, so changing DS mid-asm is harmless in a flat model.
     */
    __asm__ volatile (
        "movl %0, %%eax      \n"
        "movw %%ax, %%ds     \n"
        "movw %%ax, %%es     \n"
        "movw %%ax, %%fs     \n"
        "movw %%ax, %%gs     \n"
        "pushl %0            \n"   /* SS  = GDT_USER_DATA | 3 */
        "pushl %1            \n"   /* ESP = user_stack_top    */
        "pushfl              \n"   /* EFLAGS (current)        */
        "orl  $0x200, (%%esp)\n"   /* ensure IF = 1           */
        "pushl %2            \n"   /* CS  = GDT_USER_CODE | 3 */
        "pushl %3            \n"   /* EIP = fn                */
        "iret                \n"
        :
        : "r"((uint32_t)GDT_USER_DATA),
          "r"(user_stack_top),
          "r"((uint32_t)GDT_USER_CODE),
          "r"((uint32_t)fn)
        : "eax", "memory"
    );
    __builtin_unreachable();
}
