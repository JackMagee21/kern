#ifndef PMM_H
#define PMM_H

#include <stddef.h>
#include <stdint.h>

#define PMM_FRAME_SIZE  4096u
#define PMM_FRAME_SHIFT 12u

/*
 * Initialise the PMM from the Multiboot memory map.
 * kernel_end: first byte after the kernel image (_kernel_end from linker).
 * All frames below 1 MB and those covering the kernel image are pre-reserved.
 */
void pmm_init(uint32_t mbi_addr, uint32_t kernel_end);

/*
 * Allocate / free a single 4 KB physical frame.
 * pmm_alloc_frame returns the physical base address, or 0 on failure.
 */
uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t addr);

/* Reserve a physical range (marks frames as used without tracking them as
 * "total" memory — use after pmm_init to protect the heap region). */
void pmm_reserve(uint32_t base, size_t len);

size_t pmm_get_free(void);   /* free frames */
size_t pmm_get_total(void);  /* total usable frames (from mmap) */

#endif
