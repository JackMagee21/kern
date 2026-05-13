/*
 * switch_context(uint32_t *old_esp_ptr, uint32_t new_esp)
 *
 * Cooperative kernel context switch.
 *
 * Saves the current task's callee-saved registers and EFLAGS onto its kernel
 * stack, then stores the resulting ESP in *old_esp_ptr.  Loads new_esp as the
 * new stack pointer and restores the next task's context.
 *
 * Stack layout when called (cdecl, before any push):
 *   [esp+0]  return address
 *   [esp+4]  old_esp_ptr    (arg 1)
 *   [esp+8]  new_esp        (arg 2)
 *
 * After five pushes (5 × 4 = 20 bytes) the args are at +24 and +28.
 *
 * New-task initial stack frame (set up by task_alloc in task.c):
 *   sp+0:  eflags (0x200 — IF=1)  ← t->esp points here; popped first by popf
 *   sp+4:  ebp = 0
 *   sp+8:  edi = 0
 *   sp+12: esi = 0
 *   sp+16: ebx = 0
 *   sp+20: task entry-point fn    ← popped last by ret
 */

.global switch_context
.type   switch_context, @function

switch_context:
    push %ebx
    push %esi
    push %edi
    push %ebp
    pushf

    mov  24(%esp), %eax     /* eax = old_esp_ptr */
    mov  %esp,     (%eax)   /* *old_esp_ptr = current ESP */

    mov  28(%esp), %esp     /* load new task's ESP */

    popf
    pop  %ebp
    pop  %edi
    pop  %esi
    pop  %ebx
    ret

.size switch_context, . - switch_context

/*
 * fork_enter_user(registers_t *r)
 *
 * Restores full user-mode register state from a registers_t struct and
 * performs a privilege-level IRET to ring 3.  Used by the fork trampoline
 * so the child resumes at exactly the point the parent called fork(), with
 * eax already set to 0 by the caller.
 *
 * registers_t field offsets (idt.h):
 *   [+0]  ds
 *   [+4]  edi  [+8] esi  [+12] ebp  [+16] esp(k)  [+20] ebx  [+24] edx
 *   [+28] ecx  [+32] eax
 *   [+36] int_no  [+40] err_code
 *   [+44] eip  [+48] cs  [+52] eflags  [+56] useresp  [+60] ss
 */
.global fork_enter_user
.type   fork_enter_user, @function

fork_enter_user:
    movl 4(%esp), %esi          /* esi = registers_t * */

    /* Switch to user data segments. */
    movl 0(%esi), %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Push iret frame (ring-change form: ss, useresp, eflags, cs, eip). */
    movl 60(%esi), %eax; pushl %eax   /* ss */
    movl 56(%esi), %eax; pushl %eax   /* useresp */
    movl 52(%esi), %eax
    orl  $0x200, %eax                 /* ensure IF=1 in child */
    pushl %eax                        /* eflags */
    movl 48(%esi), %eax; pushl %eax   /* cs */
    movl 44(%esi), %eax; pushl %eax   /* eip */

    /* Push popa frame in reverse pop order (eax first, edi last). */
    /* popa reads: edi, esi, ebp, [esp skipped], ebx, edx, ecx, eax */
    movl 32(%esi), %eax; pushl %eax   /* eax (=0: fork returns 0 in child) */
    movl 28(%esi), %eax; pushl %eax   /* ecx */
    movl 24(%esi), %eax; pushl %eax   /* edx */
    movl 20(%esi), %eax; pushl %eax   /* ebx */
    pushl $0                           /* esp placeholder (popa discards it) */
    movl 12(%esi), %eax; pushl %eax   /* ebp */
    movl  8(%esi), %eax; pushl %eax   /* esi (saved user esi) */
    movl  4(%esi), %eax; pushl %eax   /* edi */

    popa
    iret

.size fork_enter_user, . - fork_enter_user
