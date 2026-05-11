#include "vmm.h"
#include "pmm.h"
#include <stdint.h>

/*
 * Kernel page directory and the two pre-allocated page tables that cover
 * the first 8 MB (PDE[0] = 0–4 MB, PDE[1] = 4–8 MB).
 *
 * Anything beyond 8 MB starts as not-present; vmm_map() adds new page
 * tables on demand by calling pmm_alloc_frame().
 */
static uint32_t page_dir[1024]     __attribute__((aligned(4096)));
static uint32_t pt_low[2][1024]    __attribute__((aligned(4096)));

#define PDE_KERNEL (VMM_PRESENT | VMM_WRITABLE)
#define PTE_KERNEL (VMM_PRESENT | VMM_WRITABLE)

/* Given a present PDE value, return a pointer to its page table.
 * Works because we are identity-mapped: physical == virtual. */
static inline uint32_t *pde_to_pt(uint32_t pde) {
    return (uint32_t *)(uintptr_t)(pde & ~0xFFFu);
}

void vmm_init(void) {
    /* Clear the page directory — all regions start as not-present. */
    for (int i = 0; i < 1024; i++) page_dir[i] = 0;

    /* Wire up the two pre-allocated tables for the first 8 MB. */
    for (int t = 0; t < 2; t++) {
        page_dir[t] = (uint32_t)(uintptr_t)pt_low[t] | PDE_KERNEL;

        /* Fill each table with an identity mapping (frame == page index). */
        for (int p = 0; p < 1024; p++) {
            uint32_t phys = (uint32_t)((t * 1024 + p) << 12);
            pt_low[t][p]  = phys | PTE_KERNEL;
        }
    }

    /*
     * Unmap page 0 (physical 0x0000–0x0FFF).
     * Any null-pointer access will now fault cleanly instead of
     * corrupting the BIOS interrupt vector table.
     */
    pt_low[0][0] = 0;

    /* Clear CR4.PSE — we are using 4 KB pages, not 4 MB PSE pages. */
    __asm__ volatile (
        "mov %%cr4, %%eax  \n"
        "and $~0x10, %%eax \n"   /* clear bit 4 (PSE) */
        "mov %%eax, %%cr4  \n"
        ::: "eax"
    );

    /* Load the page directory into CR3. */
    __asm__ volatile ("mov %0, %%cr3" :: "r"((uint32_t)(uintptr_t)page_dir) :);

    /* Enable paging: set CR0.PG (bit 31). */
    __asm__ volatile (
        "mov %%cr0,          %%eax \n"
        "or  $0x80000000,    %%eax \n"
        "mov %%eax,          %%cr0 \n"
        ::: "eax"
    );
}

void vmm_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FFu;

    /* Allocate a page table for this PDE if one doesn't exist yet. */
    if (!(page_dir[pdi] & VMM_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_frame();
        if (!pt_phys) return; /* PMM exhausted */

        /* Zero the new page table (identity-mapped, so phys == virt). */
        uint32_t *pt = (uint32_t *)(uintptr_t)pt_phys;
        for (int i = 0; i < 1024; i++) pt[i] = 0;

        page_dir[pdi] = pt_phys | PDE_KERNEL;
    }

    uint32_t *pt = pde_to_pt(page_dir[pdi]);
    pt[pti] = (phys & ~0xFFFu) | (flags & 0xFFFu);

    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_unmap(uint32_t virt) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FFu;

    if (!(page_dir[pdi] & VMM_PRESENT)) return;

    pde_to_pt(page_dir[pdi])[pti] = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

uint32_t vmm_get_cr3(void) {
    return (uint32_t)(uintptr_t)page_dir;
}
