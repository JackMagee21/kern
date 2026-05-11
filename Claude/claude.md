# Kern 2.0 — State Summary

## Stack
x86 (i686), 32-bit protected mode, GRUB Multiboot, no stdlib, cross-compiled with i686-elf-gcc.

## Directory Layout
```
boot/      entry point (_start), stack, passes eax/ebx to kernel_main
cpu/       GDT, IDT (256 gates), ISR/IRQ stubs, PIC remap, io.h (inb/outb)
drivers/   VGA (0xB8000, 80×25), PS/2 keyboard (circular buf), PIT timer (1 kHz)
kernel/    kernel_main, kprintf (%c %s %d %u %x %X), shell (readline + dispatch)
lib/       kmemset, kmemcpy, kstrlen, kstrcpy, kstrcmp, kstrncmp
mm/        PMM (bitmap), VMM (PSE paging), heap (linked-list kmalloc/kfree)
```

## Boot Sequence
```
terminal_init → GDT → PIC → IDT → keyboard → timer → sti
→ PMM (parse Multiboot mmap) → vmm_init (enable paging) → heap_init
→ shell_run  [does not return]
```

## Subsystems

**GDT** — null / kernel-code (0x9A) / kernel-data (0x92), all 4 GB flat, loaded via `gdt_flush.s`.

**IDT** — 32 exception + 16 IRQ gates; per-vector handler table; `isr_dispatch` sends EOI.

**PMM** (`mm/pmm.c`) — 128 KB bitmap (1 bit/4 KB frame, full 4 GB). Parses Multiboot mmap. Reserves first 1 MB + kernel image. `pmm_alloc_frame()` linear-scans from frame 256. `pmm_reserve()` blocks ranges from allocation.

**VMM** (`mm/vmm.c`) — CR4.PSE=1, 1024-entry page directory, each PDE = 4 MB identity-mapped. Loads CR3, sets CR0.PG. Virtual == physical everywhere.

**Heap** (`mm/heap.c`) — 2 MB region at `_kernel_end`. Block header: magic, size, free, prev, next. First-fit `kmalloc`, forward+backward coalesce on `kfree`. Region reserved in PMM.

**Shell commands** — `help`, `clear`, `echo`, `ticks`, `meminfo`, `version`, `halt`.

## Key Addresses / Symbols
| Symbol / Address | Meaning |
|---|---|
| `0x00100000` | Kernel load address (GRUB Multiboot) |
| `_kernel_end` | First byte after kernel image (linker symbol) |
| `_kernel_end + 0` | Heap start (2 MB) |
| `0xB8000` | VGA text framebuffer |
| `0x20–0x2F` | Remapped IRQ vectors |

## Future Developments (priority order)
1. **Page-fault handler** — catch #PF (vector 14), print fault address from CR2.
2. **Higher-half kernel** — remap kernel to 0xC0000000; adjust linker script and boot stub.
3. **Fine-grained VMM** — 4 KB pages, `vmm_map(virt, phys, flags)` / `vmm_unmap`, per-process CR3.
4. **User mode** — ring-3 segments in GDT, TSS, `iret` to user EIP, privilege separation.
5. **System calls** — `int 0x80` gate (or `sysenter`), syscall table, basic ABI.
6. **ELF loader** — parse ELF32 headers, map PT_LOAD segments into a new address space.
7. **initrd / filesystem** — GRUB module as a tar/CPIO archive; VFS shim (`open/read/close`).
8. **Scheduler** — round-robin task list, context switch (save/restore registers + CR3 swap).
