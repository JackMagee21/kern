#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>

/*
 * Load an ELF32 executable into a task's address space.
 *
 * elf_data : pointer to the raw ELF bytes in kernel memory.
 * pd_phys  : physical address of the task's page directory
 *            (pre-created with vmm_create_pd).
 *
 * Maps each PT_LOAD segment into pd_phys.  Allocates a 16 KB user stack
 * at USER_STACK_BASE..USER_STACK_TOP.
 *
 * Returns the ELF entry point (virtual), or 0 on error.
 * *out_brk is set to the page-aligned end of the last loaded segment
 * (the initial program break for sbrk).
 */
uint32_t elf_load(const void *elf_data, uint32_t pd_phys, uint32_t *out_brk);

/*
 * Build an argc/argv frame on the user stack of an already-loaded task.
 *
 * cmdline : space-separated argument string (e.g. "echo hello world").
 *           The first token becomes argv[0] and the program name.
 * pd_phys : physical address of the task's page directory.
 *
 * Writes the strings and argv[] pointer array onto the user stack pages via
 * vmm_virt_to_phys so no PD switch is required.
 *
 * Returns the initial user ESP the task should start with (points to the
 * fake return address / argc / argv layout expected by cdecl _start).
 */
uint32_t elf_setup_argv(uint32_t pd_phys, const char *cmdline);

#endif
