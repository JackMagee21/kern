#include "tmpfs.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>

static tmpfs_entry_t entries[TMPFS_MAX_FILES];

void tmpfs_init(void) {
    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++)
        entries[i].used = 0;
}

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void kstrncpy(char *dst, const char *src, uint32_t n) {
    uint32_t i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

tmpfs_entry_t *tmpfs_find(const char *name) {
    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++)
        if (entries[i].used && kstrcmp(entries[i].name, name) == 0)
            return &entries[i];
    return NULL;
}

tmpfs_entry_t *tmpfs_create(const char *name) {
    /* Reuse existing entry if present (truncate). */
    tmpfs_entry_t *e = tmpfs_find(name);
    if (e) {
        e->size = 0; /* truncate */
        return e;
    }
    /* Find a free slot. */
    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++) {
        if (!entries[i].used) {
            e = &entries[i];
            kstrncpy(e->name, name, TMPFS_NAME_MAX);
            e->data = (uint8_t *)kmalloc(TMPFS_INIT_CAP);
            if (!e->data) return NULL;
            e->size = 0;
            e->cap  = TMPFS_INIT_CAP;
            e->used = 1;
            return e;
        }
    }
    return NULL; /* table full */
}

tmpfs_entry_t *tmpfs_get_by_idx(uint32_t idx) {
    uint32_t found = 0;
    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++) {
        if (entries[i].used) {
            if (found == idx) return &entries[i];
            found++;
        }
    }
    return NULL;
}

uint32_t tmpfs_write(tmpfs_entry_t *e, const void *buf, uint32_t len,
                     uint32_t pos) {
    if (!e || !buf || !len) return 0;

    uint32_t end = pos + len;

    /* Grow backing buffer if needed. */
    if (end > e->cap) {
        uint32_t new_cap = e->cap * 2;
        while (new_cap < end) new_cap *= 2;
        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return 0;
        for (uint32_t i = 0; i < e->size; i++) new_data[i] = e->data[i];
        kfree(e->data);
        e->data = new_data;
        e->cap  = new_cap;
    }

    /* Zero-fill any gap between current size and write position. */
    for (uint32_t i = e->size; i < pos; i++) e->data[i] = 0;

    /* Copy data. */
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) e->data[pos + i] = src[i];

    if (end > e->size) e->size = end;
    return len;
}
