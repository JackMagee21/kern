#ifndef SYSCALL_H
#define SYSCALL_H

/* Syscall numbers — eax convention for int 0x80 */
#define SYS_EXIT  0
#define SYS_WRITE 1

void syscall_init(void);

#endif
