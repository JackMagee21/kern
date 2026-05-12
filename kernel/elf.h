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

#endif
