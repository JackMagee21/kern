#ifndef SYSCALL_H
#define SYSCALL_H

/* Syscall numbers — eax = number, ebx/ecx/edx = args 1-3, return in eax */
#define SYS_EXIT    0   /* exit()                                            */
#define SYS_WRITE   1   /* write(fd, buf, len) — fd 1 = VGA stdout          */
#define SYS_OPEN    2   /* open(path) → fd                                   */
#define SYS_READ    3   /* read(fd, buf, len) — fd 0 = keyboard              */
#define SYS_CLOSE   4   /* close(fd)                                         */
#define SYS_GETPID  5   /* getpid() → pid                                    */
#define SYS_SBRK    6   /* sbrk(incr) → old brk                             */
#define SYS_FORK    7   /* fork() → child pid / 0                           */
#define SYS_WAITPID 8   /* waitpid(pid) → pid or -1                         */
#define SYS_EXEC    9   /* exec(path, cmdline) → replaces image             */
#define SYS_SLEEP   10  /* sleep(ms)                                        */
#define SYS_PIPE    11  /* pipe(fds[2]) — fds[0]=read, fds[1]=write        */
#define SYS_DUP2    12  /* dup2(old_fd, new_fd)                             */
#define SYS_GETDENT 13  /* getdent(idx, buf, bufsz) → name len (0 = done)  */

void syscall_init(void);

#endif
