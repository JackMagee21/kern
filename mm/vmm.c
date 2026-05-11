#include "vmm.h"
#include <stdint.h>

/*
 * Kernel page directory — 1024 entries, each describing a 4 MB region when
 * PSE is enabled.  Must be 4 KB aligned (guaranteed by __attribute__((aligned))).
 */
static uint32_t page_dir[1024] __attribute__((aligned(4096)));

void vmm_init(void) {
    /* Enable PSE (Page Size Extension) in CR4 so bit 7 of a PDE means 4 MB. */
    __asm__ volatile (
        "mov %%cr4, %%eax   \n"
        "or  $0x10,  %%eax  \n"   /* CR4.PSE = bit 4 */
        "mov %%eax, %%cr4   \n"
        ::: "eax"
    );

    /*
     * Identity-map the full 4 GB (all 1024 × 4 MB regions).
     * PDE[i] maps virtual [i*4M, (i+1)*4M) → physical [i*4M, (i+1)*4M).
     * Flags: present + writable + PSE.
     */
    for (uint32_t i = 0; i < 1024; i++)
        page_dir[i] = (i << 22) | VMM_PSE | VMM_WRITABLE | VMM_PRESENT;

    /* Load CR3 with the physical address of our page directory. */
    __asm__ volatile ("mov %0, %%cr3" :: "r"((uint32_t)page_dir) :);

    /* Enable paging: set CR0.PG (bit 31). */
    __asm__ volatile (
        "mov %%cr0,         %%eax  \n"
        "or  $0x80000000,   %%eax  \n"
        "mov %%eax,         %%cr0  \n"
        ::: "eax"
    );
}

uint32_t vmm_get_cr3(void) {
    return (uint32_t)page_dir;
}
