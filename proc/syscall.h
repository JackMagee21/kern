#ifndef SYSCALL_H
#define SYSCALL_H

/* Syscall numbers — eax convention for int 0x80 */
#define SYS_EXIT   0
#define SYS_WRITE  1   /* write(fd=ebx, buf=ecx, len=edx) — fd 1 = stdout */
#define SYS_OPEN   2   /* open(path=ebx) → fd                              */
#define SYS_READ   3   /* read(fd=ebx, buf=ecx, len=edx) → bytes read      */
#define SYS_CLOSE  4   /* close(fd=ebx)                                    */
#define SYS_GETPID 5   /* getpid() → pid in eax                            */
#define SYS_SBRK   6   /* sbrk(increment=ebx) → old brk in eax            */

void syscall_init(void);

#endif
