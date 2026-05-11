# Kern 2.0 — State Summary

## Stack
x86 (i686), 32-bit protected mode, GRUB Multiboot, no stdlib, cross-compiled with i686-elf-gcc.

## Directory Layout
```
boot/      entry point (_start), 16 KB stack, passes eax/ebx to kernel_main
cpu/       GDT (6 entries), IDT (256 gates), ISR/IRQ stubs, PIC, io.h,
           TSS, exception handlers, context_switch.s
drivers/   VGA (80×25, terminal_set_color), PS/2 keyboard, PIT 1 kHz (tick callback),
           serial COM1 (115200 8N1, mirrors all terminal_putchar output)
kernel/    kernel_main, kprintf, shell, kpanic (red-screen register dump)
lib/       kmemset, kmemcpy, kstrlen, kstrcpy, kstrcmp, kstrncmp
mm/        multiboot.h, PMM (bitmap), VMM (4 KB pages), heap (kmalloc/kfree)
proc/      task.h/c (task struct, create/yield/sleep/exit), scheduler.h/c,
           syscall.h/c (int 0x80 dispatch, SYS_EXIT=0 SYS_WRITE=1),
           usermode.h/c (enter_usermode — iret to ring-3)
```

## Boot Sequence
```
terminal_init → GDT (+ TSS) → PIC → IDT → exceptions_init
→ keyboard → timer (1 kHz) → sti
→ PMM (Multiboot mmap) → vmm_init (4 KB pages) → heap_init + pmm_reserve
→ scheduler_init (PID 0 = kernel/shell)
→ syscall_init (registers int 0x80 handler, DPL=3 gate)
→ shell_run  [does not return]
```

## Subsystems

**GDT** (6 entries) — null / kernel-code (0x9A) / kernel-data (0x92) /
user-code (0xFA, DPL=3) / user-data (0xF2, DPL=3) / TSS (0x89).
Selectors: kernel-code=0x08, kernel-data=0x10, user-code=0x1B, user-data=0x23, TSS=0x28.

**TSS** (`cpu/tss.c`) — esp0/ss0 set to kernel data segment. iomap_base = sizeof(tss_t) (no I/O bitmap). `tss_set_kernel_stack(esp)` updates esp0 per-task before ring-3 entry.

**Exception handlers** (`cpu/exceptions.c`) — all 32 vectors (0–31) call `kpanic`.
**kpanic** (`kernel/panic.c`) — CLI, white-on-red screen, exception name table, full register dump, CR2 + error-code decode for #PF (#14).

**PMM** (`mm/pmm.c`) — 128 KB uint32 bitmap, 1 bit/4 KB frame. Parses Multiboot mmap. Reserves 0–1 MB and kernel image. `pmm_alloc_frame()` / `pmm_free_frame()` / `pmm_reserve()`.

**VMM** (`mm/vmm.c`) — 4 KB pages (PSE disabled). Two pre-allocated page tables cover 0–8 MB with VMM_USER set (ring-3 accessible for testing). **Page 0 unmapped** (null guard). `vmm_map(virt, phys, flags)` lazily allocates page tables from PMM; `vmm_unmap` + `invlpg`.

**Heap** (`mm/heap.c`) — 2 MB at `_kernel_end`. First-fit linked-list. Forward+backward coalesce. Reserved in PMM.

**Scheduler** (`proc/scheduler.c`) — **preemptive** round-robin, 10 ms quantum. `scheduler_init()` wraps boot context as PID 0. `scheduler_tick()` (called by PIT IRQ0 every ms): wakes sleeping tasks, then on quantum expiry calls `task_yield()` if another READY task exists. EOI is sent *before* the handler in `isr_dispatch` so the PIC is unmasked before any context switch.

**Tasks** (`proc/task.c`) — `task_create(name, fn)` kmallocs a `task_t` (8 KB stack), builds initial stack frame. `task_yield()` finds next READY task, calls `switch_context`. `task_sleep(ms)` marks SLEEPING + yields; if no other task is ready, spins with `hlt` until timer wakes it. `task_exit()` marks DEAD + yields.

**context_switch** (`cpu/context_switch.s`) — saves ebx/esi/edi/ebp/eflags onto current stack, stores ESP in `*old_esp_ptr`, loads new ESP, restores and `ret` into new task.

## Key Addresses / Symbols
| Symbol / Address | Meaning |
|---|---|
| `0x00000000–0x00000FFF` | **Not mapped** — null-pointer guard |
| `0x00001000–0x007FFFFF` | Identity-mapped (first 8 MB) |
| `0x00100000` | Kernel load address |
| `_kernel_end` | First byte past kernel image |
| `_kernel_end + 0` | Heap start (2 MB) |
| `0x000B8000` | VGA text framebuffer |
| `0x20–0x2F` | Remapped IRQ vectors |
| `0x28` | TSS selector |

**Syscalls** (`proc/syscall.c`) — `int 0x80` gate (DPL=3, vector 0x80). Dispatch on `eax`: 0=exit (→ task_exit), 1=write (ebx = `const char *` → terminal_print). Return value in eax via `regs->eax`.

**User mode** (`proc/usermode.c`) — `enter_usermode(fn, user_stack_top)`: sets TSS.esp0 to top of current task's kernel stack, switches DS/ES/FS/GS to GDT_USER_DATA, builds iret frame (SS/ESP/EFLAGS+IF/CS/EIP), executes `iret`. Never returns. First 8 MB pages carry VMM_USER so kernel-mapped ring-3 code can execute.

## Shell Commands
`help` `clear` `echo` `ticks` `meminfo` `ps` `sleep <ms>` `usertest` `version` `halt`

`usertest` — spawns a kernel task that immediately calls `enter_usermode`; the user function prints via SYS_WRITE then calls SYS_EXIT, and control returns to the scheduler.

## Future Developments (priority order)
1. **Per-task page directories** — each task gets its own CR3; kernel identity-mapped in upper half of every PD.
3. **ELF loader** — parse ELF32 headers, map PT_LOAD segments into a new page directory.
4. **Higher-half kernel** — remap kernel to 0xC0000000; update linker, boot stub, VMM.
5. **initrd / VFS** — GRUB module as tar/CPIO; `open`/`read`/`close` shim.
