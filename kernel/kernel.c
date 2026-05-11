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

/* Defined by the linker script — first byte after the kernel image. */
extern uint32_t _kernel_end;

/* 2 MB kernel heap placed immediately after the kernel binary. */
#define HEAP_SIZE (2u * 1024u * 1024u)

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    terminal_init();
    serial_init();  /* mirror all terminal output to COM1 */

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

    __asm__ volatile ("sti");
    terminal_print("[OK] Interrupts enabled\n");

    /* Verify Multiboot magic before touching the info structure. */
    if (magic != MULTIBOOT_MAGIC) {
        terminal_print("[ERR] Not loaded by a Multiboot bootloader — halting.\n");
        __asm__ volatile ("cli; hlt");
        __builtin_unreachable();
    }

    /* Physical memory manager. */
    pmm_init(mbi_addr, (uint32_t)&_kernel_end);
    kprintf("[OK] PMM ready     — %u MB free (%u frames)\n",
            (uint32_t)((pmm_get_free() * 4) / 1024),
            (uint32_t)pmm_get_free());

    /* Paging: 4 KB identity-mapped pages, page 0 unmapped. */
    vmm_init();
    terminal_print("[OK] Paging enabled  (4 KB pages, page 0 guard)\n");

    /* Heap: 2 MB region right after the kernel image. */
    uint32_t heap_start = (uint32_t)&_kernel_end;
    heap_init(heap_start, HEAP_SIZE);
    pmm_reserve(heap_start, HEAP_SIZE);
    kprintf("[OK] Heap ready    — %u KB at 0x%x\n",
            (uint32_t)(HEAP_SIZE / 1024), heap_start);

    /* Cooperative task scheduler (wraps the boot context as PID 0). */
    scheduler_init();
    terminal_print("[OK] Scheduler ready\n");

    /* int 0x80 syscall gate (SYS_EXIT=0, SYS_WRITE=1). */
    syscall_init();
    terminal_print("[OK] Syscalls ready  (int 0x80)\n");

    shell_run(); /* does not return */
}
