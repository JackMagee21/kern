#include "elf.h"      /* kernel/elf.h  — loader declaration      */
#include "elf32.h"   /* mm/elf32.h    — Elf32_Ehdr, Elf32_Phdr  */
#include "vmm.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/* ── Private helpers ───────────────────────────────────────────────────── */

static void kfill(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = (uint8_t)c;
}

static void kcopy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/* ── elf_load ──────────────────────────────────────────────────────────── */

uint32_t elf_load(const void *elf_data, uint32_t pd_phys, uint32_t *out_brk) {
    const Elf32_Ehdr *hdr = (const Elf32_Ehdr *)elf_data;

    /* Validate ELF magic, type, and architecture. */
    if (*(const uint32_t *)hdr->e_ident != ELF_MAGIC) return 0;
    if (hdr->e_type    != ET_EXEC)  return 0;
    if (hdr->e_machine != EM_386)   return 0;

    const Elf32_Phdr *phdrs =
        (const Elf32_Phdr *)((const uint8_t *)elf_data + hdr->e_phoff);

    uint32_t brk = 0; /* tracks highest virtual byte loaded */

    /* ── Load each PT_LOAD segment ──────────────────────────────────────── */
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        uint32_t vaddr  = ph->p_vaddr;
        uint32_t filesz = ph->p_filesz;
        uint32_t memsz  = ph->p_memsz;
        const uint8_t *src = (const uint8_t *)elf_data + ph->p_offset;

        uint32_t flags = VMM_PRESENT | VMM_USER;
        if (ph->p_flags & PF_W) flags |= VMM_WRITABLE;

        uint32_t start_page = vaddr & ~0xFFFu;
        uint32_t end_page   = (vaddr + memsz + 0xFFFu) & ~0xFFFu;

        for (uint32_t page = start_page; page < end_page; page += 0x1000u) {
            uint32_t frame_phys = pmm_alloc_frame();
            if (!frame_phys) return 0;

            uint8_t *frame = (uint8_t *)(uintptr_t)P2V(frame_phys);
            kfill(frame, 0, 0x1000u);

            /* Copy the portion of file data that falls in this page. */
            uint32_t seg_file_end = vaddr + filesz;
            uint32_t page_end     = page + 0x1000u;

            if (vaddr < page_end && seg_file_end > page) {
                uint32_t copy_start = (vaddr  > page)     ? vaddr        : page;
                uint32_t copy_end   = (seg_file_end < page_end) ? seg_file_end : page_end;
                uint32_t dst_off    = copy_start - page;
                uint32_t src_off    = copy_start - vaddr;
                kcopy(frame + dst_off, src + src_off, copy_end - copy_start);
            }

            vmm_map_in_pd(pd_phys, page, frame_phys, flags);
        }

        uint32_t seg_end = (vaddr + memsz + 0xFFFu) & ~0xFFFu;
        if (seg_end > brk) brk = seg_end;
    }

    /* ── Allocate user stack ────────────────────────────────────────────── */
    uint32_t stk_flags = VMM_PRESENT | VMM_WRITABLE | VMM_USER;
    for (uint32_t p = USER_STACK_BASE; p < USER_STACK_TOP; p += 0x1000u) {
        uint32_t f = pmm_alloc_frame();
        if (!f) return 0;
        kfill((void *)(uintptr_t)P2V(f), 0, 0x1000u);
        vmm_map_in_pd(pd_phys, p, f, stk_flags);
    }

    if (out_brk) *out_brk = brk;
    return hdr->e_entry;
}
