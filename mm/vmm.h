#ifndef VMM_H
#define VMM_H

#include <stdint.h>

/* PDE / PTE flag bits */
#define VMM_PRESENT  (1u << 0)
#define VMM_WRITABLE (1u << 1)
#define VMM_USER     (1u << 2)   /* accessible from ring 3 */

/*
 * Set up 4 KB paging.
 *
 * The first 8 MB are identity-mapped (virtual == physical), except page 0
 * (0x0000–0x0FFF) which is intentionally left unmapped so that null-pointer
 * dereferences trigger a #PF instead of silently succeeding.
 *
 * Everything outside 0x1000–0x7FFFFF is not present; accesses there produce
 * a page fault, which the exception handler will catch and report.
 */
void vmm_init(void);

/*
 * Map one 4 KB page: virtual address virt → physical frame phys.
 * flags should be a combination of VMM_PRESENT, VMM_WRITABLE, VMM_USER.
 * If the containing page table does not exist yet, one frame is allocated
 * from the PMM.
 */
void vmm_map(uint32_t virt, uint32_t phys, uint32_t flags);

/*
 * Remove the mapping for virtual address virt and flush the TLB entry.
 * Does nothing if the address was not mapped.
 */
void vmm_unmap(uint32_t virt);

/* Physical address of the current page directory (for future CR3 reloads). */
uint32_t vmm_get_cr3(void);

#endif
