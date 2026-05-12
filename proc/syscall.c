#include "syscall.h"
#include "idt.h"
#include "task.h"
#include "vfs.h"
#include "vga.h"
#include "vmm.h"
#include "pmm.h"
#include "keyboard.h"
#include <stdint.h>

static void syscall_handler(registers_t *regs) {
    switch (regs->eax) {

        /* SYS_EXIT (0): terminate the calling task. */
        case SYS_EXIT:
            task_exit();

        /* SYS_WRITE (1): write bytes to a file descriptor.
         *   ebx = fd  (1 = stdout)
         *   ecx = virtual address of buffer
         *   edx = byte count
         *   Returns bytes written in eax, or (uint32_t)-1 on error. */
        case SYS_WRITE: {
            uint32_t fd  = regs->ebx;
            const char *buf = (const char *)(uintptr_t)regs->ecx;
            uint32_t len = regs->edx;
            if (fd == 1) {
                for (uint32_t i = 0; i < len; i++) terminal_putchar(buf[i]);
                regs->eax = len;
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        /* SYS_OPEN (2): open a file by name.
         *   ebx = virtual address of null-terminated path string.
         *   Returns: fd index (0-7) in eax, or (uint32_t)-1 on error. */
        case SYS_OPEN: {
            task_t *t = task_current();
            const char *path = (const char *)(uintptr_t)regs->ebx;
            vfs_file_t *f = vfs_open(path);
            if (!f) { regs->eax = (uint32_t)-1; break; }

            uint32_t fd;
            for (fd = 0; fd < TASK_MAX_FDS; fd++) {
                if (!t->fds[fd]) { t->fds[fd] = f; break; }
            }
            if (fd == TASK_MAX_FDS) { vfs_close(f); regs->eax = (uint32_t)-1; break; }
            regs->eax = fd;
            break;
        }

        /* SYS_READ (3): read bytes from an open file descriptor.
         *   ebx = fd  (0 = stdin), ecx = buffer virtual address, edx = byte count.
         *   Returns bytes read in eax, or (uint32_t)-1 on error.
         *   fd 0: line-buffered keyboard input (echoes characters, stops at '\n'). */
        case SYS_READ: {
            uint32_t fd  = regs->ebx;
            char    *buf = (char *)(uintptr_t)regs->ecx;
            uint32_t len = regs->edx;

            if (fd == 0) {
                /* int 0x80 clears IF; re-enable so keyboard IRQ and timer can fire. */
                __asm__ volatile ("sti");
                uint32_t n = 0;
                while (n < len) {
                    char c = keyboard_getchar();
                    if (c == '\b') {
                        if (n > 0) { n--; terminal_putchar('\b'); terminal_putchar(' '); terminal_putchar('\b'); }
                        continue;
                    }
                    terminal_putchar(c);
                    buf[n++] = c;
                    if (c == '\n') break;
                }
                regs->eax = n;
                break;
            }

            task_t *t = task_current();
            if (fd >= TASK_MAX_FDS || !t->fds[fd]) {
                regs->eax = (uint32_t)-1; break;
            }
            regs->eax = vfs_read(t->fds[fd], buf, len);
            break;
        }

        /* SYS_CLOSE (4): close a file descriptor.
         *   ebx = fd. */
        case SYS_CLOSE: {
            task_t *t = task_current();
            uint32_t fd = regs->ebx;
            if (fd < TASK_MAX_FDS && t->fds[fd]) {
                vfs_close(t->fds[fd]);
                t->fds[fd] = NULL;
            }
            regs->eax = 0;
            break;
        }

        /* SYS_GETPID (5): return the calling task's PID. */
        case SYS_GETPID:
            regs->eax = task_current()->pid;
            break;

        /* SYS_SBRK (6): grow the user heap by `increment` bytes.
         *   ebx = increment (signed; only positive growth is supported).
         *   Returns the old program break in eax, or (uint32_t)-1 on error. */
        case SYS_SBRK: {
            task_t  *t   = task_current();
            int32_t  inc = (int32_t)regs->ebx;
            uint32_t old = t->brk;

            if (inc <= 0) { regs->eax = old; break; }

            uint32_t new_brk = old + (uint32_t)inc;
            /* Map any new pages between old and new brk. */
            uint32_t p = (old + 0xFFFu) & ~0xFFFu; /* first unmapped page */
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

        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}

void syscall_init(void) {
    idt_register_handler(0x80, syscall_handler);
}
