#ifndef VMM_H
#define VMM_H

#include <stdint.h>

/* ── Address-space layout ──────────────────────────────────────────────── */

#define KERNEL_VMA   0xC0000000u   /* kernel is linked to run here         */
#define USER_VM_TOP  0xC0000000u   /* first byte of kernel in every PD     */

/* Convert between virtual kernel addresses and physical addresses.
 * Valid only for addresses within the pre-mapped kernel window. */
#define V2P(v)  ((uint32_t)(v) - KERNEL_VMA)
#define P2V(p)  ((uint32_t)(p) + KERNEL_VMA)

/* ── PDE / PTE flag bits ───────────────────────────────────────────────── */

#define VMM_PRESENT  (1u << 0)
#define VMM_WRITABLE (1u << 1)
#define VMM_USER     (1u << 2)   /* accessible from ring 3 */

/* User stack layout (placed just below the kernel boundary). */
#define USER_STACK_TOP   0xBFFF0000u   /* initial user SP             */
#define USER_STACK_BASE  0xBFFEC000u   /* bottom of 16 KB user stack  */

/* ── Kernel VMM ────────────────────────────────────────────────────────── */

/*
 * Set up 4 KB paging.
 *
 * Pre-maps 32 MB starting at KERNEL_VMA (8 page tables × 4 MB).
 * Replaces the 4 MB PSE boot mapping installed by boot.s and reloads CR3.
 * Page 0 (physical 0x000–0xFFF) is left unmapped as a null-pointer guard.
 */
void vmm_init(void);

/* Map one 4 KB page: virtual virt → physical phys in the CURRENT PD. */
void vmm_map(uint32_t virt, uint32_t phys, uint32_t flags);

/* Remove the mapping for virtual address virt and flush the TLB entry. */
void vmm_unmap(uint32_t virt);

/* Physical address of the kernel's page directory (for task->cr3). */
uint32_t vmm_get_phys_pd(void);

/* ── Per-task page directories (Phase 2) ──────────────────────────────── */

/*
 * Allocate a new page directory for a user task.
 * The kernel half (PDE[768-1023]) is cloned from the kernel PD so that
 * kernel code and data are reachable from every task's address space.
 * Returns the PHYSICAL address of the new PD, or 0 on failure.
 */
uint32_t vmm_create_pd(void);

/* Free a task's page directory, all user-space page tables, and all mapped
 * user page frames.  Kernel-half PDEs are NOT freed (they are shared). */
void vmm_destroy_pd(uint32_t pd_phys);

/*
 * Deep-copy a page directory for fork().
 * Kernel half (PDE[768-1023]) is shared (same physical page tables).
 * User half (PDE[0-767]) is fully cloned: new page tables and new page frames,
 * with the same content as the source.
 * Returns the physical address of the new PD, or 0 on allocation failure.
 */
uint32_t vmm_clone_pd(uint32_t src_phys);

/*
 * Map one 4 KB page in an ARBITRARY page directory (not the current one).
 * Used by the ELF loader to populate a new task's address space while
 * still running in the kernel's address space.
 */
void vmm_map_in_pd(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags);

/* Load a page directory by writing its physical address to CR3. */
void vmm_switch(uint32_t pd_phys);

/*
 * Translate a virtual address in an arbitrary PD to a physical address.
 * Returns 0 if the address is not mapped.  Handles both user and kernel PDEs.
 */
uint32_t vmm_virt_to_phys(uint32_t pd_phys, uint32_t virt);

#endif
