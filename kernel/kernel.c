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

/* Defined by the linker script — first byte after the kernel image. */
extern uint32_t _kernel_end;

/* 2 MB kernel heap, placed immediately after the kernel binary. */
#define HEAP_SIZE (2u * 1024u * 1024u)

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    terminal_init();

    gdt_init();
    terminal_print("[OK] GDT loaded\n");

    pic_init();
    terminal_print("[OK] PIC remapped\n");

    idt_init();
    terminal_print("[OK] IDT loaded\n");

    exceptions_init();
    terminal_print("[OK] Exception handlers registered\n");

    keyboard_init();
    terminal_print("[OK] Keyboard ready\n");

    timer_init(1000);
    terminal_print("[OK] Timer ready\n");

    __asm__ volatile ("sti");
    terminal_print("[OK] Interrupts enabled\n");

    /* Verify we were launched by a Multiboot-compliant bootloader. */
    if (magic != MULTIBOOT_MAGIC) {
        terminal_print("[ERR] Not loaded by a Multiboot bootloader — halting.\n");
        __asm__ volatile ("cli; hlt");
        __builtin_unreachable();
    }

    /* Physical memory manager — parses the Multiboot mmap. */
    pmm_init(mbi_addr, (uint32_t)&_kernel_end);
    kprintf("[OK] PMM ready  — %u MB free (%u frames)\n",
            (uint32_t)((pmm_get_free() * 4) / 1024),
            (uint32_t)pmm_get_free());

    /* Virtual memory — identity-map 4 GB with 4 MB PSE pages, enable paging. */
    vmm_init();
    terminal_print("[OK] Paging enabled (identity-mapped)\n");

    /* Heap — 2 MB region starting at the first byte past the kernel image.
     * We also tell the PMM to reserve those frames so pmm_alloc_frame()
     * won't accidentally return addresses inside the heap. */
    uint32_t heap_start = (uint32_t)&_kernel_end;
    heap_init(heap_start, HEAP_SIZE);
    pmm_reserve(heap_start, HEAP_SIZE);
    kprintf("[OK] Heap ready — %u KB at 0x%x\n",
            (uint32_t)(HEAP_SIZE / 1024), heap_start);

    shell_run(); /* does not return */
}
