# Kern 2.0 — State Summary

## Stack
x86 (i686), 32-bit protected mode, GRUB Multiboot, no stdlib, cross-compiled with i686-elf-gcc.

## Directory Layout
```
boot/      entry point (_start), stack, passes eax/ebx to kernel_main
cpu/       GDT, IDT (256 gates), ISR/IRQ stubs, PIC remap, io.h, exception handlers
drivers/   VGA (0xB8000, 80×25, terminal_set_color), PS/2 keyboard, PIT timer (1 kHz)
kernel/    kernel_main, kprintf, shell, kpanic (register-dump panic screen)
lib/       kmemset, kmemcpy, kstrlen, kstrcpy, kstrcmp, kstrncmp
mm/        PMM (bitmap), VMM (4 KB pages), heap (linked-list kmalloc/kfree)
```

## Boot Sequence
```
terminal_init → GDT → PIC → IDT → exceptions_init → keyboard → timer → sti
→ PMM (parse Multiboot mmap) → vmm_init (4 KB paging) → heap_init + pmm_reserve
→ shell_run  [does not return]
```

## Subsystems

**GDT** — null / kernel-code (0x9A) / kernel-data (0x92), flat 4 GB, loaded via `gdt_flush.s`.

**IDT** — 32 exception + 16 IRQ gates. Per-vector handler table; `isr_dispatch` sends EOI.

**Exception handlers** (`cpu/exceptions.c`) — All 32 CPU exceptions (0–31) registered via `exceptions_init()`. Each calls `kpanic()`.

**kpanic** (`kernel/panic.c`) — Disables interrupts, switches VGA to white-on-red, prints exception name, error code, full register dump (EAX–EIP/EFLAGS/CS/DS). For #14 (Page Fault) also reads CR2 and decodes the error code (present/write/user bits). Halts permanently.

**PMM** (`mm/pmm.c`) — 128 KB uint32 bitmap (1 bit/4 KB frame). Parses Multiboot mmap. Reserves first 1 MB + kernel image. `pmm_alloc_frame()` / `pmm_free_frame()` / `pmm_reserve()`.

**VMM** (`mm/vmm.c`) — 4 KB pages (PSE disabled). Pre-allocates two 4 KB page tables covering 0–8 MB and identity-maps them. **Page 0 (0x0000–0x0FFF) is intentionally unmapped** — null-pointer dereferences trigger #PF. `vmm_map(virt, phys, flags)` allocates a new page table from the PMM on demand; `vmm_unmap(virt)` clears the PTE and calls `invlpg`.

**Heap** (`mm/heap.c`) — 2 MB at `_kernel_end`. First-fit linked-list; forward+backward coalesce on free. Region reserved in PMM.

**Shell** — `help`, `clear`, `echo`, `ticks`, `meminfo`, `version`, `halt`.

## Key Addresses / Symbols
| Symbol / Address | Meaning |
|---|---|
| `0x00000000–0x00000FFF` | **Not mapped** (null-pointer guard) |
| `0x00001000–0x007FFFFF` | Identity-mapped (first 8 MB) |
| `0x00100000` | Kernel load address |
| `_kernel_end` | First byte past kernel image |
| `_kernel_end + 0` | Heap start (2 MB) |
| `0x000B8000` | VGA text framebuffer |
| `0x20–0x2F` | Remapped IRQ vectors |

## Future Developments (priority order)
1. **Higher-half kernel** — remap kernel to 0xC0000000; update linker script, boot stub, and VMM. Standard layout for user-space separation.
2. **User mode** — ring-3 GDT segments, TSS, `iret` to user EIP.
3. **System calls** — `int 0x80` gate, syscall dispatch table, basic ABI (write, exit).
4. **ELF loader** — parse ELF32, map PT_LOAD segments into a new page directory.
5. **Scheduler** — round-robin task list, context-switch (save/restore regs + CR3 swap).
6. **initrd / VFS** — GRUB module as tar/CPIO; `open`/`read`/`close` shim.
