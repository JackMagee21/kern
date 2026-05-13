#ifndef SYSCALL_H
#define SYSCALL_H

/* Syscall numbers — eax convention for int 0x80 */
#define SYS_EXIT    0
#define SYS_WRITE   1   /* write(fd=ebx, buf=ecx, len=edx) — fd 1 = stdout */
#define SYS_OPEN    2   /* open(path=ebx) → fd                              */
#define SYS_READ    3   /* read(fd=ebx, buf=ecx, len=edx) → bytes read      */
#define SYS_CLOSE   4   /* close(fd=ebx)                                    */
#define SYS_GETPID  5   /* getpid() → pid in eax                            */
#define SYS_SBRK    6   /* sbrk(increment=ebx) → old brk in eax            */
#define SYS_FORK    7   /* fork() → child pid in parent, 0 in child        */
#define SYS_WAITPID 8   /* waitpid(pid=ebx) → pid or -1                    */
#define SYS_EXEC    9   /* exec(path=ebx) → replaces current image         */
#define SYS_SLEEP   10  /* sleep(ms=ebx)                                   */

void syscall_init(void);

#endif
