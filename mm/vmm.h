#ifndef VMM_H
#define VMM_H

#include <stdint.h>

/* Page-directory / page-table entry flag bits */
#define VMM_PRESENT  (1u << 0)
#define VMM_WRITABLE (1u << 1)
#define VMM_USER     (1u << 2)
#define VMM_PSE      (1u << 7)  /* 4 MB page (set in PDE when PSE is on) */

/*
 * Set up an identity-mapped page directory using 4 MB PSE pages and
 * enable paging.  After this call, virtual address == physical address
 * for all of the 4 GB address space.
 */
void vmm_init(void);

/* Physical address of the page directory (useful for future CR3 reloads). */
uint32_t vmm_get_cr3(void);

#endif
