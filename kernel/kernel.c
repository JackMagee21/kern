#include <stdint.h>
#include <stddef.h>
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "timer.h"
#include "printf.h"
#include "shell.h"
#include "multiboot.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "exceptions.h"
#include "panic.h"
#include "scheduler.h"
#include "serial.h"
#include "syscall.h"
#include "task.h"
#include "vfs.h"
#include "ata.h"
#include "fat16.h"

extern uint32_t _kernel_end;   /* VMA of first byte after the kernel image */

#define HEAP_SIZE (2u * 1024u * 1024u)

void kernel_main(uint32_t magic, uint32_t mbi_phys) {
    terminal_init();
    serial_init();

    gdt_init();
    terminal_print("[OK] GDT loaded  (ring-0, ring-3, TSS)\n");

    pic_init();
    terminal_print("[OK] PIC remapped\n");

    idt_init();
    terminal_print("[OK] IDT loaded\n");

    exceptions_init();
    terminal_print("[OK] Exception handlers registered\n");

    keyboard_init();
    terminal_print("[OK] Keyboard ready\n");

    timer_init(1000);
    terminal_print("[OK] Timer ready  (1 kHz)\n");

    if (magic != MULTIBOOT_MAGIC) {
        terminal_print("[ERR] Not loaded by a Multiboot bootloader — halting.\n");
        __asm__ volatile ("cli; hlt");
        __builtin_unreachable();
    }

    /*
     * Switch from the 4 MB PSE boot mapping to proper 4 KB pages.
     * Interrupts are kept off until vmm_init() finishes: a timer IRQ
     * between zeroing PDE[768] and reloading CR3 would triple-fault.
     */
    vmm_init();
    terminal_print("[OK] Paging enabled  (4 KB pages, page 0 guard)\n");

    __asm__ volatile ("sti");
    terminal_print("[OK] Interrupts enabled\n");

    /*
     * mbi_phys is the physical address GRUB stored in ebx.
     * The multiboot info structure lives in low physical memory (< 1 MB),
     * which is now only accessible via P2V.
     */
    multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)P2V(mbi_phys);
    uint32_t kernel_phys_end = V2P((uint32_t)&_kernel_end);
    pmm_init((uint32_t)mbi, kernel_phys_end);
    kprintf("[OK] PMM ready     — %u MB free (%u frames)\n",
            (uint32_t)((pmm_get_free() * 4) / 1024),
            (uint32_t)pmm_get_free());

    /*
     * Heap: 2 MB starting after the kernel image AND any GRUB modules.
     * GRUB loads the initrd right after the kernel in physical memory;
     * starting the heap before it would cause kmalloc to overwrite it.
     */
    uint32_t heap_phys = kernel_phys_end;
    if ((mbi->flags & MULTIBOOT_FLAG_MODS) && mbi->mods_count > 0) {
        multiboot_mod_t *mods =
            (multiboot_mod_t *)(uintptr_t)P2V(mbi->mods_addr);
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            uint32_t end = (mods[i].mod_end + 0xFFFu) & ~0xFFFu; /* page-align */
            if (end > heap_phys) heap_phys = end;
        }
    }
    uint32_t heap_virt = (uint32_t)P2V(heap_phys);
    heap_init(heap_virt, HEAP_SIZE);
    pmm_reserve(heap_phys, HEAP_SIZE);
    kprintf("[OK] Heap ready    — %u KB at 0x%x (phys 0x%x)\n",
            (uint32_t)(HEAP_SIZE / 1024), heap_virt, heap_phys);

    /* Load the initrd (now safe — heap starts after it). */
    if ((mbi->flags & MULTIBOOT_FLAG_MODS) && mbi->mods_count > 0) {
        multiboot_mod_t *mods =
            (multiboot_mod_t *)(uintptr_t)P2V(mbi->mods_addr);
        uint32_t mod_phys = mods[0].mod_start;
        uint32_t mod_size = mods[0].mod_end - mod_phys;
        vfs_init((const void *)(uintptr_t)P2V(mod_phys), mod_size);
        pmm_reserve(mod_phys, mod_size);
        kprintf("[OK] Initrd loaded — %u bytes at phys 0x%x\n",
                mod_size, mod_phys);
    }

    /* ATA primary master + FAT16 (disk.img passed as -drive to QEMU). */
    if (ata_init() == 0) {
        terminal_print("[OK] ATA drive detected\n");
        if (fat16_init() == 0)
            terminal_print("[OK] FAT16 filesystem mounted\n");
        else
            terminal_print("[--] FAT16 not found on drive\n");
    } else {
        terminal_print("[--] No ATA drive — using tmpfs only\n");
    }

    scheduler_init();
    terminal_print("[OK] Scheduler ready  (preemptive, 10 ms quantum)\n");

    syscall_init();
    terminal_print("[OK] Syscalls ready  (int 0x80)\n");

    /*
     * Try to launch the userland shell ("sh") from the initrd.
     * If it exists, run it and wait; fall back to the kernel shell on exit.
     */
    {
        vfs_file_t *f = vfs_open("sh");
        if (f) {
            uint32_t sz  = f->size;
            void    *buf = kmalloc(sz);
            if (buf) {
                vfs_read(f, buf, sz);
                vfs_close(f);
                task_t *sh = task_exec("sh", buf, sz);
                kfree(buf);
                if (sh) {
                    terminal_print("[OK] Launching userland shell (sh)\n");
                    task_wait(sh);
                    terminal_print("\nUserland shell exited — dropping to kernel shell.\n");
                }
            } else {
                vfs_close(f);
            }
        }
    }

    shell_run();
}
