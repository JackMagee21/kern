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
 * New-task initial stack frame (set up by task_create):
 *   sp+0:  eflags (0x200 — IF=1)
 *   sp+4:  ebp = 0
 *   sp+8:  edi = 0
 *   sp+12: esi = 0
 *   sp+16: ebx = 0
 *   sp+20: task entry-point fn   ← popped by ret as the return address
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
