#include "syscall.h"
#include "signal.h"
#include "idt.h"
#include "task.h"
#include "pipe.h"
#include "vfs.h"
#include "vga.h"
#include "vmm.h"
#include "pmm.h"
#include "keyboard.h"
#include "heap.h"
#include "elf.h"
#include "usermode.h"
#include <stdint.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

/* Find a free fd slot in t (fd >= first_free). Returns TASK_MAX_FDS if none. */
static uint32_t alloc_fd(task_t *t, uint32_t first_free) {
    for (uint32_t i = first_free; i < TASK_MAX_FDS; i++)
        if (t->fds[i].type == FD_NONE) return i;
    return TASK_MAX_FDS;
}

/* ── main handler ─────────────────────────────────────────────────────────── */

static void syscall_handler(registers_t *regs) {
    switch (regs->eax) {

    /* SYS_EXIT (0) */
    case SYS_EXIT:
        task_exit();

    /* SYS_WRITE (1): write(fd, buf, len) */
    case SYS_WRITE: {
        uint32_t    fd  = regs->ebx;
        const char *buf = (const char *)(uintptr_t)regs->ecx;
        uint32_t    len = regs->edx;

        if (fd >= TASK_MAX_FDS) { regs->eax = (uint32_t)-1; break; }

        task_t *t = task_current();
        fd_t   *f = &t->fds[fd];

        if (f->type == FD_NONE) {
            /* fd 1 = VGA stdout default; all others are errors. */
            if (fd == 1) {
                for (uint32_t i = 0; i < len; i++) terminal_putchar(buf[i]);
                regs->eax = len;
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        if (f->type == FD_FILE_W) {
            regs->eax = vfs_write(f->file, buf, len);
            break;
        }

        if (f->type == FD_PIPE_W) {
            /* Blocking write: yield until there is space or no readers. */
            uint32_t written = 0;
            while (written < len) {
                if (!f->pipe->readers) {
                    /* Broken pipe — deliver SIGPIPE and return error. */
                    task_signal(task_current(), SIGPIPE);
                    regs->eax = (uint32_t)-1; goto write_done;
                }
                if (task_current()->pending_sigs) { regs->eax = (uint32_t)-1; goto write_done; }
                int32_t n = pipe_write(f->pipe, buf + written, len - written);
                if (n > 0) { written += (uint32_t)n; continue; }
                __asm__ volatile ("sti");
                task_yield();
            }
            regs->eax = written;
        } else {
            regs->eax = (uint32_t)-1;
        }
    write_done: break;
    }

    /* SYS_OPEN (2): open(path) → fd */
    case SYS_OPEN: {
        task_t     *t    = task_current();
        const char *path = (const char *)(uintptr_t)regs->ebx;
        vfs_file_t *file = vfs_open(path);
        if (!file) { regs->eax = (uint32_t)-1; break; }

        uint32_t fd = alloc_fd(t, 0);
        if (fd == TASK_MAX_FDS) { vfs_close(file); regs->eax = (uint32_t)-1; break; }

        t->fds[fd].type = FD_FILE;
        t->fds[fd].file = file;
        regs->eax = fd;
        break;
    }

    /* SYS_READ (3): read(fd, buf, len) */
    case SYS_READ: {
        uint32_t  fd  = regs->ebx;
        char     *buf = (char *)(uintptr_t)regs->ecx;
        uint32_t  len = regs->edx;

        if (fd >= TASK_MAX_FDS) { regs->eax = (uint32_t)-1; break; }

        task_t *t = task_current();
        fd_t   *f = &t->fds[fd];

        if (f->type == FD_NONE) {
            if (fd == 0) {
                __asm__ volatile ("sti");
                if (len == 1) {
                    /* Raw single-char read: no echo, no buffering.
                     * Used by the shell readline for interactive line editing. */
                    buf[0] = keyboard_getchar();
                    regs->eax = 1;
                } else {
                    /* Cooked read: echo + newline-terminate. */
                    uint32_t n = 0;
                    while (n < len) {
                        char c = keyboard_getchar();
                        if (c == '\b') {
                            if (n > 0) {
                                n--;
                                terminal_putchar('\b');
                                terminal_putchar(' ');
                                terminal_putchar('\b');
                            }
                            continue;
                        }
                        terminal_putchar(c);
                        buf[n++] = c;
                        if (c == '\n') break;
                    }
                    regs->eax = n;
                }
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        if (f->type == FD_FILE) {
            regs->eax = vfs_read(f->file, buf, len);
            break;
        }

        if (f->type == FD_PIPE_R) {
            /* Blocking read: yield until data, EOF, or signal. */
            while (f->pipe->len == 0) {
                if (!f->pipe->writers || task_current()->pending_sigs) {
                    regs->eax = 0; goto read_done;
                }
                __asm__ volatile ("sti");
                task_yield();
            }
            regs->eax = (uint32_t)pipe_read(f->pipe, buf, len);
        } else {
            regs->eax = (uint32_t)-1;
        }
    read_done: break;
    }

    /* SYS_CLOSE (4): close(fd) */
    case SYS_CLOSE: {
        task_t  *t  = task_current();
        uint32_t fd = regs->ebx;
        if (fd < TASK_MAX_FDS && t->fds[fd].type != FD_NONE)
            task_fd_close(&t->fds[fd]);
        regs->eax = 0;
        break;
    }

    /* SYS_GETPID (5) */
    case SYS_GETPID:
        regs->eax = task_current()->pid;
        break;

    /* SYS_SBRK (6): sbrk(increment) → old brk */
    case SYS_SBRK: {
        task_t  *t   = task_current();
        int32_t  inc = (int32_t)regs->ebx;
        uint32_t old = t->brk;

        if (inc <= 0) { regs->eax = old; break; }

        uint32_t new_brk = old + (uint32_t)inc;
        uint32_t p = (old + 0xFFFu) & ~0xFFFu;
        uint32_t flags = VMM_PRESENT | VMM_WRITABLE | VMM_USER;
        for (; p < new_brk; p += 0x1000u) {
            uint32_t frame = pmm_alloc_frame();
            if (!frame) { regs->eax = (uint32_t)-1; goto sbrk_done; }
            vmm_map_in_pd(t->cr3, p, frame, flags);
            __asm__ volatile ("invlpg (%0)" :: "r"(p) : "memory");
        }
        t->brk = new_brk;
        regs->eax = old;
      sbrk_done: break;
    }

    /* SYS_FORK (7): fork() → child pid / 0 */
    case SYS_FORK: {
        task_t *child = task_fork(regs);
        regs->eax = child ? child->pid : (uint32_t)-1;
        break;
    }

    /* SYS_WAITPID (8): waitpid(pid) → pid or -1 */
    case SYS_WAITPID: {
        regs->eax = (uint32_t)task_waitpid((uint32_t)regs->ebx);
        break;
    }

    /* SYS_EXEC (9): exec(path, cmdline)
     *   ebx = path (null-terminated)
     *   ecx = cmdline (space-separated args string; NULL → use path as argv[0])
     * Replaces the calling task's image.  Does not return on success. */
    case SYS_EXEC: {
        const char *path    = (const char *)(uintptr_t)regs->ebx;
        const char *cmdline = regs->ecx ? (const char *)(uintptr_t)regs->ecx
                                        : path;

        vfs_file_t *vf = vfs_open(path);
        if (!vf) { regs->eax = (uint32_t)-1; break; }

        uint32_t size = vf->size;
        void    *buf  = kmalloc(size);
        if (!buf) { vfs_close(vf); regs->eax = (uint32_t)-1; break; }
        vfs_read(vf, buf, size);
        vfs_close(vf);

        uint32_t new_pd = vmm_create_pd();
        if (!new_pd) { kfree(buf); regs->eax = (uint32_t)-1; break; }

        uint32_t brk   = 0;
        uint32_t entry = elf_load(buf, new_pd, &brk);
        kfree(buf);
        if (!entry) { vmm_destroy_pd(new_pd); regs->eax = (uint32_t)-1; break; }

        task_t  *t      = task_current();
        uint32_t old_pd = t->cr3;

        /* Switch to new PD, free old user address space. */
        t->cr3 = new_pd;
        vmm_switch(new_pd);
        vmm_destroy_pd(old_pd);

        t->brk            = brk;
        t->user_entry     = entry;
        t->user_stack_top = elf_setup_argv(new_pd, cmdline);

        /* Close fds 2+ on exec; keep stdin (0) and stdout (1). */
        for (uint32_t i = 2; i < TASK_MAX_FDS; i++)
            task_fd_close(&t->fds[i]);

        enter_usermode((void (*)(void))entry, t->user_stack_top);
        __builtin_unreachable();
    }

    /* SYS_SLEEP (10): sleep(ms) */
    case SYS_SLEEP:
        task_sleep((uint32_t)regs->ebx);
        regs->eax = 0;
        break;

    /* SYS_PIPE (11): pipe(fds_ptr)
     *   ebx = pointer to int[2] in user space; filled with [read_fd, write_fd].
     * Returns 0 on success, -1 on failure. */
    case SYS_PIPE: {
        task_t  *t   = task_current();
        int32_t *out = (int32_t *)(uintptr_t)regs->ebx;

        pipe_t *p = pipe_alloc();
        if (!p) { regs->eax = (uint32_t)-1; break; }

        uint32_t rfd = alloc_fd(t, 0);
        if (rfd == TASK_MAX_FDS) { pipe_close_reader(p); pipe_close_writer(p);
                                   regs->eax = (uint32_t)-1; break; }
        uint32_t wfd = alloc_fd(t, rfd + 1);
        if (wfd == TASK_MAX_FDS) { pipe_close_reader(p); pipe_close_writer(p);
                                   regs->eax = (uint32_t)-1; break; }

        t->fds[rfd].type = FD_PIPE_R; t->fds[rfd].pipe = p;
        t->fds[wfd].type = FD_PIPE_W; t->fds[wfd].pipe = p;

        out[0] = (int32_t)rfd;
        out[1] = (int32_t)wfd;
        regs->eax = 0;
        break;
    }

    /* SYS_DUP2 (12): dup2(old_fd, new_fd)
     * Closes new_fd if open, then makes new_fd refer to the same file/pipe. */
    case SYS_DUP2: {
        task_t  *t      = task_current();
        uint32_t old_fd = regs->ebx;
        uint32_t new_fd = regs->ecx;

        if (old_fd >= TASK_MAX_FDS || new_fd >= TASK_MAX_FDS) {
            regs->eax = (uint32_t)-1; break;
        }
        if (old_fd == new_fd) { regs->eax = (int32_t)new_fd; break; }

        /* Close destination if occupied. */
        if (t->fds[new_fd].type != FD_NONE)
            task_fd_close(&t->fds[new_fd]);

        t->fds[new_fd] = task_fd_dup(t->fds[old_fd]);
        regs->eax = (int32_t)new_fd;
        break;
    }

    /* SYS_GETDENT (13): getdent(idx, buf, bufsz)
     * Copies the filename of the idx-th initrd entry into buf.
     * Returns name length on success, 0 if idx is out of range. */
    case SYS_GETDENT: {
        uint32_t  idx   = regs->ebx;
        char     *buf   = (char *)(uintptr_t)regs->ecx;
        uint32_t  bufsz = regs->edx;
        const char *name = vfs_getent(idx);
        if (!name) { regs->eax = 0; break; }
        uint32_t len = 0;
        while (name[len] && len < bufsz - 1) { buf[len] = name[len]; len++; }
        buf[len] = '\0';
        regs->eax = len;
        break;
    }

    /* SYS_KILL (14): kill(pid, sig) */
    case SYS_KILL: {
        uint32_t pid = regs->ebx;
        int      sig = (int)regs->ecx;
        task_t  *head = task_list_head();
        if (!head) { regs->eax = (uint32_t)-1; break; }
        task_t *t = head;
        do {
            if (t->pid == pid) { task_signal(t, sig); regs->eax = 0; goto kill_done; }
            t = t->next;
        } while (t != head);
        regs->eax = (uint32_t)-1;
    kill_done: break;
    }

    /* SYS_SIGNAL (15): signal(sig, SIG_DFL/SIG_IGN) → old disposition */
    case SYS_SIGNAL: {
        int      sig  = (int)regs->ebx;
        uint32_t disp = regs->ecx; /* SIG_DFL=0, SIG_IGN=1 */
        if (sig < 1 || sig >= NSIG) { regs->eax = (uint32_t)-1; break; }
        task_t *t = task_current();
        regs->eax = t->sig_action[sig];
        t->sig_action[sig] = disp;
        break;
    }

    /* SYS_SETFG (16): setfg(pid) — set foreground task for Ctrl+C (0=clear) */
    case SYS_SETFG: {
        uint32_t pid = regs->ebx;
        if (pid == 0) { task_set_fg(NULL); regs->eax = 0; break; }
        task_t *head = task_list_head();
        task_t *t = head;
        if (head) do {
            if (t->pid == pid) { task_set_fg(t); regs->eax = 0; goto setfg_done; }
            t = t->next;
        } while (t != head);
        regs->eax = (uint32_t)-1;
    setfg_done: break;
    }

    /* SYS_OPEN2 (17): open2(path, flags)
     * flags: O_RDONLY=0, O_WRONLY|O_CREAT=open for writing (truncate),
     *        O_WRONLY|O_CREAT|O_APPEND=open for appending. */
    case SYS_OPEN2: {
        task_t     *t     = task_current();
        const char *path  = (const char *)(uintptr_t)regs->ebx;
        uint32_t    flags = regs->ecx;

        uint32_t fd = alloc_fd(t, 0);
        if (fd == TASK_MAX_FDS) { regs->eax = (uint32_t)-1; break; }

        if (flags & O_WRONLY) {
            vfs_file_t *vf = (flags & O_APPEND) ? vfs_open_append(path)
                                                  : vfs_create(path);
            if (!vf) { regs->eax = (uint32_t)-1; break; }
            t->fds[fd].type = FD_FILE_W;
            t->fds[fd].file = vf;
        } else {
            /* Read-only: same as SYS_OPEN */
            vfs_file_t *vf = vfs_open(path);
            if (!vf) { regs->eax = (uint32_t)-1; break; }
            t->fds[fd].type = FD_FILE;
            t->fds[fd].file = vf;
        }
        regs->eax = fd;
        break;
    }

    default:
        regs->eax = (uint32_t)-1;
        break;
    }

    /* Deliver any pending signals before returning to user space. */
    signals_deliver();
}

void syscall_init(void) {
    idt_register_handler(0x80, syscall_handler);
}
