#include "syscall.h"
#include "idt.h"
#include "task.h"
#include "vfs.h"
#include "vga.h"
#include <stdint.h>

static void syscall_handler(registers_t *regs) {
    switch (regs->eax) {

        /* SYS_EXIT (0): terminate the calling task. */
        case SYS_EXIT:
            task_exit();

        /* SYS_WRITE (1): write a null-terminated string to the terminal.
         *   ebx = virtual address of string (in caller's address space). */
        case SYS_WRITE:
            terminal_print((const char *)(uintptr_t)regs->ebx);
            regs->eax = 0;
            break;

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
         *   ebx = fd, ecx = buffer virtual address, edx = byte count.
         *   Returns bytes read in eax, or (uint32_t)-1 on error. */
        case SYS_READ: {
            task_t *t = task_current();
            uint32_t fd = regs->ebx;
            if (fd >= TASK_MAX_FDS || !t->fds[fd]) {
                regs->eax = (uint32_t)-1; break;
            }
            void *buf = (void *)(uintptr_t)regs->ecx;
            regs->eax = vfs_read(t->fds[fd], buf, regs->edx);
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

        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}

void syscall_init(void) {
    idt_register_handler(0x80, syscall_handler);
}
