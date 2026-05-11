#include "pmm.h"
#include "multiboot.h"
#include <stdint.h>
#include <stddef.h>

/*
 * Bitmap physical-memory manager.
 *
 * One bit per 4 KB frame.  4 GB / 4 KB = 1 M frames → 128 KB bitmap.
 * Bit = 0 → frame is free.  Bit = 1 → frame is used / reserved.
 *
 * The bitmap lives in BSS (inside the kernel image) so it is automatically
 * reserved when pmm_init marks the kernel region as used.
 */

#define MAX_FRAMES  (1u << 20)          /* 4 GB / 4 KB */
#define BITMAP_WORDS (MAX_FRAMES / 32)  /* 32 frames per uint32 word */

static uint32_t bitmap[BITMAP_WORDS];  /* 128 KB, zero-initialised (BSS) */
static size_t   total_frames = 0;
static size_t   free_frames  = 0;

/* ── Low-level bit operations ─────────────────────────────────────────── */

static inline void frame_set(uint32_t f)   { bitmap[f >> 5] |=  (1u << (f & 31)); }
static inline void frame_clear(uint32_t f) { bitmap[f >> 5] &= ~(1u << (f & 31)); }
static inline int  frame_test(uint32_t f)  { return (bitmap[f >> 5] >> (f & 31)) & 1; }

/* ── Region helpers ───────────────────────────────────────────────────── */

/* Mark every frame that overlaps [base, base+len) as used. */
static void region_mark_used(uint32_t base, uint32_t len) {
    uint32_t f     = base >> PMM_FRAME_SHIFT;
    uint32_t f_end = (base + len + PMM_FRAME_SIZE - 1) >> PMM_FRAME_SHIFT;
    for (; f < f_end && f < MAX_FRAMES; f++) {
        if (!frame_test(f)) {
            frame_set(f);
            if (free_frames) free_frames--;
        }
    }
}

/*
 * Mark every full frame contained within [base, base+len) as free.
 * Partial frames at either end are left as-is (conservative).
 */
static void region_mark_free(uint32_t base, uint32_t len) {
    /* Round base UP to next page, end DOWN */
    uint32_t f     = (base + PMM_FRAME_SIZE - 1) >> PMM_FRAME_SHIFT;
    uint32_t f_end = (base + len) >> PMM_FRAME_SHIFT;
    for (; f < f_end && f < MAX_FRAMES; f++) {
        if (frame_test(f)) {
            frame_clear(f);
            free_frames++;
            total_frames++;
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

void pmm_init(uint32_t mbi_addr, uint32_t kernel_end) {
    /* Start with all frames marked used (bitmap is already all-zero in BSS,
     * so set every word to 0xFFFFFFFF). */
    for (size_t i = 0; i < BITMAP_WORDS; i++) bitmap[i] = 0xFFFFFFFFu;
    total_frames = 0;
    free_frames  = 0;

    multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)mbi_addr;

    if (!(mbi->flags & MULTIBOOT_FLAG_MMAP))
        return; /* no map — all memory stays reserved */

    /* Pass 1: mark available regions free. */
    uint32_t off = 0;
    while (off < mbi->mmap_length) {
        multiboot_mmap_entry_t *e =
            (multiboot_mmap_entry_t *)(uintptr_t)(mbi->mmap_addr + off);

        if (e->type == MULTIBOOT_MMAP_AVAILABLE && e->addr < 0x100000000ULL) {
            uint32_t base = (uint32_t)e->addr;
            uint64_t end64 = e->addr + e->len;
            uint32_t end   = (end64 >= 0x100000000ULL)
                             ? 0xFFFFFFFFu : (uint32_t)end64;
            if (end > base)
                region_mark_free(base, end - base);
        }
        off += e->size + 4; /* size field doesn't include itself */
    }

    /* Pass 2: re-mark the regions we must never hand out. */
    region_mark_used(0,         0x100000);              /* first 1 MB */
    region_mark_used(0x100000,  kernel_end - 0x100000); /* kernel image */
    /* (The bitmap itself is inside the kernel image, so pass 2 covers it.) */
}

uint32_t pmm_alloc_frame(void) {
    if (!free_frames) return 0;

    /* Linear scan — start above the first 1 MB to skip always-reserved frames. */
    for (uint32_t w = 256 / 32; w < BITMAP_WORDS; w++) {
        if (bitmap[w] == 0xFFFFFFFFu) continue; /* all used, skip word */
        for (uint32_t bit = 0; bit < 32; bit++) {
            uint32_t f = w * 32 + bit;
            if (!frame_test(f)) {
                frame_set(f);
                free_frames--;
                return f << PMM_FRAME_SHIFT;
            }
        }
    }
    return 0;
}

void pmm_free_frame(uint32_t addr) {
    uint32_t f = addr >> PMM_FRAME_SHIFT;
    if (f >= MAX_FRAMES || !frame_test(f)) return;
    frame_clear(f);
    free_frames++;
}

void pmm_reserve(uint32_t base, size_t len) {
    region_mark_used(base, (uint32_t)len);
}

size_t pmm_get_free(void)  { return free_frames;  }
size_t pmm_get_total(void) { return total_frames; }
