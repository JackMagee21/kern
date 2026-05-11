#include "heap.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Linked-list heap allocator.
 *
 * Memory layout:
 *   [ block_t header | <user data> ] [ block_t header | <user data> ] ...
 *
 * All allocations are rounded up to 8 bytes so headers stay naturally
 * aligned.  The header itself is padded to a multiple of 8 bytes.
 */

#define HEAP_MAGIC  0xDEADB00Fu
#define ALIGN8(x)   (((size_t)(x) + 7u) & ~7u)

typedef struct block {
    uint32_t     magic;   /* sanity check */
    size_t       size;    /* usable bytes after this header */
    uint8_t      free;
    uint8_t      _pad[3];
    struct block *next;
    struct block *prev;
} block_t;

/* Round header size up to 8-byte boundary so user data starts aligned. */
#define HDR ((size_t)ALIGN8(sizeof(block_t)))

static block_t *heap_head = NULL;

/* ── Internal helpers ─────────────────────────────────────────────────── */

/* Split blk so the first `size` bytes become a new (used) allocation,
 * and the remainder (if large enough) becomes a new free block. */
static void split(block_t *blk, size_t size) {
    if (blk->size < size + HDR + 8)
        return; /* remainder too small to be useful */

    block_t *tail = (block_t *)((uint8_t *)blk + HDR + size);
    tail->magic = HEAP_MAGIC;
    tail->size  = blk->size - size - HDR;
    tail->free  = 1;
    tail->next  = blk->next;
    tail->prev  = blk;

    if (blk->next) blk->next->prev = tail;
    blk->next = tail;
    blk->size = size;
}

/* Merge blk with any immediately following free blocks, then check if the
 * previous block is also free and merge backwards. */
static void coalesce(block_t *blk) {
    /* Forward merge */
    while (blk->next && blk->next->free) {
        blk->size += HDR + blk->next->size;
        blk->next  = blk->next->next;
        if (blk->next) blk->next->prev = blk;
    }
    /* Backward merge */
    if (blk->prev && blk->prev->free) {
        block_t *p = blk->prev;
        p->size += HDR + blk->size;
        p->next  = blk->next;
        if (blk->next) blk->next->prev = p;
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

void heap_init(uint32_t start, size_t size) {
    if (size <= HDR) return;
    heap_head = (block_t *)(uintptr_t)start;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = size - HDR;
    heap_head->free  = 1;
    heap_head->next  = NULL;
    heap_head->prev  = NULL;
}

void *kmalloc(size_t size) {
    if (!size || !heap_head) return NULL;
    size = ALIGN8(size);

    for (block_t *b = heap_head; b; b = b->next) {
        if (b->free && b->size >= size) {
            split(b, size);
            b->free = 0;
            return (uint8_t *)b + HDR;
        }
    }
    return NULL; /* out of heap space */
}

void *kcalloc(size_t n, size_t size) {
    size_t total = n * size;
    uint8_t *ptr = (uint8_t *)kmalloc(total);
    if (ptr) {
        for (size_t i = 0; i < total; i++) ptr[i] = 0;
    }
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_t *blk = (block_t *)((uint8_t *)ptr - HDR);
    if (blk->magic != HEAP_MAGIC || blk->free) return;
    blk->free = 1;
    coalesce(blk);
}

size_t heap_get_used(void) {
    size_t used = 0;
    for (block_t *b = heap_head; b; b = b->next)
        if (!b->free) used += b->size;
    return used;
}

size_t heap_get_free(void) {
    size_t avail = 0;
    for (block_t *b = heap_head; b; b = b->next)
        if (b->free) avail += b->size;
    return avail;
}
