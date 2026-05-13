#include "vmm.h"
#include "pmm.h"
#include <stdint.h>

/*
 * Kernel page directory and pre-allocated page tables.
 *
 * These objects live in BSS at virtual addresses (0xC01xxxxx).
 * Their physical address = virtual - KERNEL_VMA.
 *
 * PT_PREALLOC tables × 4 MB per table = 32 MB pre-mapped at KERNEL_VMA.
 * PDE[768 + t] → pt_kernel[t] → maps physical t×4MB .. (t+1)×4MB - 1.
 */
#define PT_PREALLOC 8   /* covers 32 MB; enough for kernel + heap + page tables */

static uint32_t page_dir[1024]              __attribute__((aligned(4096)));
static uint32_t pt_kernel[PT_PREALLOC][1024] __attribute__((aligned(4096)));

/* PDE/PTE flags for the kernel window. VMM_USER lets the elf-trampoline test
 * run ring-3 code that is still mapped in kernel virtual space. */
#define PDE_KERN (VMM_PRESENT | VMM_WRITABLE | VMM_USER)
#define PTE_KERN (VMM_PRESENT | VMM_WRITABLE | VMM_USER)

/* ── Helpers ───────────────────────────────────────────────────────────── */

/* Return a pointer to the page table whose physical address is in the PDE. */
static inline uint32_t *pde_to_pt(uint32_t pde) {
    /* Physical addr stored in PDE → add KERNEL_VMA to get virtual. */
    return (uint32_t *)(uintptr_t)P2V(pde & ~0xFFFu);
}

/* ── vmm_init ──────────────────────────────────────────────────────────── */

void vmm_init(void) {
    /* Zero the entire kernel page directory. */
    for (int i = 0; i < 1024; i++) page_dir[i] = 0;

    /*
     * Map KERNEL_VMA + t×4MB → physical t×4MB for each pre-allocated table.
     * PDE index for 0xC0000000 = 0xC0000000 >> 22 = 768.
     */
    for (int t = 0; t < PT_PREALLOC; t++) {
        page_dir[768 + t] = V2P((uint32_t)pt_kernel[t]) | PDE_KERN;

        for (int p = 0; p < 1024; p++) {
            uint32_t phys = (uint32_t)((t * 1024 + p) << 12);
            pt_kernel[t][p] = phys | PTE_KERN;
        }
    }

    /* Unmap page 0 — catches null-pointer dereferences as #PF. */
    pt_kernel[0][0] = 0;

    /*
     * Load the new page directory.  CR3 write flushes the TLB, replacing
     * the 4 MB PSE boot map with our 4 KB tables.  We do NOT clear CR4.PSE
     * before this: a CR4 write also flushes the TLB, which would make the
     * PSE PDEs in the boot directory unresolvable before the new CR3 is live.
     * PSE is harmless with our new PDEs because none of them have bit 7 set.
     */
    __asm__ volatile ("mov %0, %%cr3" :: "r"(V2P((uint32_t)page_dir)) :);
    /* Paging was already enabled by boot.s; no need to touch CR0. */
}

/* ── vmm_map / vmm_unmap (current PD) ─────────────────────────────────── */

void vmm_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FFu;

    if (!(page_dir[pdi] & VMM_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_frame();
        if (!pt_phys) return;
        uint32_t *pt = (uint32_t *)(uintptr_t)P2V(pt_phys);
        for (int i = 0; i < 1024; i++) pt[i] = 0;
        page_dir[pdi] = pt_phys | PDE_KERN;
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

uint32_t vmm_get_phys_pd(void) {
    return V2P((uint32_t)page_dir);
}

/* ── Per-task page directories ─────────────────────────────────────────── */

uint32_t vmm_create_pd(void) {
    uint32_t pd_phys = pmm_alloc_frame();
    if (!pd_phys) return 0;

    uint32_t *pd = (uint32_t *)(uintptr_t)P2V(pd_phys);

    /* Zero user half — each task starts with an empty user address space. */
    for (int i = 0; i < 768; i++) pd[i] = 0;

    /* Clone kernel half so kernel code/data is reachable from this PD. */
    for (int i = 768; i < 1024; i++) pd[i] = page_dir[i];

    return pd_phys;
}

void vmm_destroy_pd(uint32_t pd_phys) {
    uint32_t *pd = (uint32_t *)(uintptr_t)P2V(pd_phys);

    for (int i = 0; i < 768; i++) {
        if (!(pd[i] & VMM_PRESENT)) continue;
        uint32_t *pt = (uint32_t *)(uintptr_t)P2V(pd[i] & ~0xFFFu);
        /* Free every mapped page frame in this table. */
        for (int j = 0; j < 1024; j++) {
            if (pt[j] & VMM_PRESENT)
                pmm_free_frame(pt[j] & ~0xFFFu);
        }
        pmm_free_frame(pd[i] & ~0xFFFu); /* free the page table itself */
    }
    pmm_free_frame(pd_phys);
}

uint32_t vmm_clone_pd(uint32_t src_phys) {
    uint32_t dst_phys = pmm_alloc_frame();
    if (!dst_phys) return 0;

    uint32_t *src = (uint32_t *)(uintptr_t)P2V(src_phys);
    uint32_t *dst = (uint32_t *)(uintptr_t)P2V(dst_phys);

    /* Kernel half: share the same physical page tables. */
    for (int i = 768; i < 1024; i++) dst[i] = src[i];

    /* User half: deep copy — new page tables and new page frames. */
    for (int i = 0; i < 768; i++) {
        if (!(src[i] & VMM_PRESENT)) { dst[i] = 0; continue; }

        uint32_t new_pt_phys = pmm_alloc_frame();
        if (!new_pt_phys) { vmm_destroy_pd(dst_phys); return 0; }

        uint32_t *src_pt = (uint32_t *)(uintptr_t)P2V(src[i] & ~0xFFFu);
        uint32_t *dst_pt = (uint32_t *)(uintptr_t)P2V(new_pt_phys);

        for (int j = 0; j < 1024; j++) {
            if (!(src_pt[j] & VMM_PRESENT)) { dst_pt[j] = 0; continue; }

            uint32_t new_frame = pmm_alloc_frame();
            if (!new_frame) {
                pmm_free_frame(new_pt_phys);
                vmm_destroy_pd(dst_phys);
                return 0;
            }
            uint8_t *sp = (uint8_t *)(uintptr_t)P2V(src_pt[j] & ~0xFFFu);
            uint8_t *dp = (uint8_t *)(uintptr_t)P2V(new_frame);
            for (int k = 0; k < 4096; k++) dp[k] = sp[k];
            dst_pt[j] = new_frame | (src_pt[j] & 0xFFFu);
        }
        dst[i] = new_pt_phys | (src[i] & 0xFFFu);
    }
    return dst_phys;
}

void vmm_map_in_pd(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FFu;
    uint32_t *pd = (uint32_t *)(uintptr_t)P2V(pd_phys);

    if (!(pd[pdi] & VMM_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_frame();
        if (!pt_phys) return;
        uint32_t *pt = (uint32_t *)(uintptr_t)P2V(pt_phys);
        for (int i = 0; i < 1024; i++) pt[i] = 0;
        pd[pdi] = pt_phys | (VMM_PRESENT | VMM_WRITABLE | VMM_USER);
    }

    uint32_t *pt = (uint32_t *)(uintptr_t)P2V(pd[pdi] & ~0xFFFu);
    pt[pti] = (phys & ~0xFFFu) | (flags & 0xFFFu);
    /* No invlpg — this PD is not the current one. */
}

void vmm_switch(uint32_t pd_phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pd_phys) : "memory");
}
