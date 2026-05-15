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
#define SYS_KILL    14  /* kill(pid, sig)                                   */
#define SYS_SIGNAL  15  /* signal(sig, disposition) → old; SIG_DFL=0, SIG_IGN=1 */
#define SYS_SETFG   16  /* setfg(pid) — foreground pid for Ctrl+C (0=clear)*/
#define SYS_OPEN2   17  /* open2(path, flags) — open with O_CREAT support  */
#define SYS_UNLINK  18  /* unlink(path) — delete a file                    */
#define SYS_LSEEK   19  /* lseek(fd, offset, whence) → new pos             */
#define SYS_STAT    20  /* stat(path, stat_buf*)                            */
#define SYS_MKDIR   21  /* mkdir(path)                                      */
#define SYS_CHDIR   22  /* chdir(path)                                      */
#define SYS_GETCWD  23  /* getcwd(buf, size)                                */

/* open2 flags */
#define O_RDONLY   0
#define O_WRONLY   1
#define O_CREAT    0x40
#define O_APPEND   0x400

void syscall_init(void);

#endif
