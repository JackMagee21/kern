#include "elf.h"      /* kernel/elf.h  — loader declaration      */
#include "elf32.h"   /* mm/elf32.h    — Elf32_Ehdr, Elf32_Phdr  */
#include "vmm.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/* ── argv setup helpers ────────────────────────────────────────────────── */

static void user_write_byte(uint32_t pd_phys, uint32_t virt, uint8_t v) {
    uint32_t phys = vmm_virt_to_phys(pd_phys, virt);
    if (phys) *((uint8_t *)(uintptr_t)P2V(phys)) = v;
}

static void user_write_u32(uint32_t pd_phys, uint32_t virt, uint32_t v) {
    for (int i = 0; i < 4; i++)
        user_write_byte(pd_phys, virt + (uint32_t)i, (uint8_t)(v >> (8 * i)));
}

static size_t kstrlen(const char *s) { size_t n = 0; while (*s++) n++; return n; }

/* Split src on spaces into tokens[]; returns token count (max MAX_ARGC). */
#define MAX_ARGC 16
#define MAX_TOKLEN 128
static int split_args(const char *src, char tokens[][MAX_TOKLEN]) {
    int n = 0;
    while (*src && n < MAX_ARGC) {
        while (*src == ' ') src++;
        if (!*src) break;
        int i = 0;
        while (*src && *src != ' ' && i < MAX_TOKLEN - 1)
            tokens[n][i++] = *src++;
        tokens[n][i] = '\0';
        n++;
    }
    return n;
}

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

/*
 * Build a cdecl-compatible argc/argv frame on the user stack.
 *
 * Stack layout written (addresses decrease downward, sp grows down):
 *   [strings packed at top of stack, null-terminated]
 *   NULL ptr          (argv sentinel)
 *   argv[argc-1] ptr
 *   ...
 *   argv[0] ptr       <- argv base
 *   argc              (uint32_t)
 *   0                 (fake return address so cdecl _start works)
 *   <-- returned ESP
 *
 * cdecl _start(int argc, char **argv):
 *   [esp+0] = fake ret = 0
 *   [esp+4] = argc
 *   [esp+8] = argv (pointer to argv[0])
 */
uint32_t elf_setup_argv(uint32_t pd_phys, const char *cmdline) {
    char tokens[MAX_ARGC][MAX_TOKLEN];
    int  argc = split_args(cmdline ? cmdline : "", tokens);
    if (argc == 0) {
        /* No args: just push a zero-argc frame. */
        uint32_t sp = USER_STACK_TOP;
        sp -= 4; user_write_u32(pd_phys, sp, 0); /* argv = NULL */
        sp -= 4; user_write_u32(pd_phys, sp, sp + 4); /* argv ptr (points to NULL) */
        sp -= 4; user_write_u32(pd_phys, sp, 0); /* argc = 0 */
        sp -= 4; user_write_u32(pd_phys, sp, 0); /* fake ret */
        return sp;
    }

    uint32_t sp = USER_STACK_TOP;

    /* Push strings from the last arg to the first (top of stack). */
    uint32_t str_ptrs[MAX_ARGC];
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = kstrlen(tokens[i]) + 1; /* include NUL */
        sp -= (uint32_t)len;
        sp &= ~3u; /* 4-byte align each string */
        for (size_t j = 0; j < len; j++)
            user_write_byte(pd_phys, sp + (uint32_t)j, (uint8_t)tokens[i][j]);
        str_ptrs[i] = sp;
    }

    sp &= ~3u; /* align before pointer array */

    /* Push NULL sentinel then argv pointers in reverse. */
    sp -= 4; user_write_u32(pd_phys, sp, 0); /* argv[argc] = NULL */
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 4;
        user_write_u32(pd_phys, sp, str_ptrs[i]);
    }

    uint32_t argv_base = sp;

    /* Push argc and fake return address. */
    sp -= 4; user_write_u32(pd_phys, sp, argv_base);   /* argv */
    sp -= 4; user_write_u32(pd_phys, sp, (uint32_t)argc); /* argc */
    sp -= 4; user_write_u32(pd_phys, sp, 0);           /* fake ret */

    return sp;
}
