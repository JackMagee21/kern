#include "vfs.h"
#include "tmpfs.h"
#include "initrd.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>

static const uint8_t *initrd_base = NULL;
static uint32_t       initrd_size = 0;

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void vfs_init(const void *initrd, uint32_t size) {
    if (!initrd || size < sizeof(initrd_header_t)) return;
    const initrd_header_t *hdr = (const initrd_header_t *)initrd;
    if (hdr->magic != INITRD_MAGIC) return;
    initrd_base = (const uint8_t *)initrd;
    initrd_size = size;
    tmpfs_init();
}

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static vfs_file_t *alloc_file(void) {
    vfs_file_t *f = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    return f;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

vfs_file_t *vfs_open(const char *name) {
    /* Search initrd first. */
    if (initrd_base) {
        const initrd_header_t *hdr = (const initrd_header_t *)initrd_base;
        const uint8_t *p   = initrd_base + sizeof(initrd_header_t);
        const uint8_t *end = initrd_base + initrd_size;

        for (uint32_t i = 0; i < hdr->count; i++) {
            if (p + sizeof(initrd_entry_t) > end) break;
            const initrd_entry_t *e = (const initrd_entry_t *)p;
            const uint8_t *data = p + sizeof(initrd_entry_t);
            if (data + e->size > end) break;

            if (kstrcmp(e->name, name) == 0) {
                vfs_file_t *f = alloc_file();
                if (!f) return NULL;
                f->src         = VFS_INITRD;
                f->pos         = 0;
                f->size        = e->size;
                f->initrd_data = data;
                return f;
            }
            p = data + e->size;
        }
    }

    /* Fall back to tmpfs. */
    tmpfs_entry_t *te = tmpfs_find(name);
    if (te) {
        vfs_file_t *f = alloc_file();
        if (!f) return NULL;
        f->src      = VFS_TMPFS;
        f->pos      = 0;
        f->size     = te->size;
        f->tmpfs_ent = te;
        return f;
    }

    return NULL;
}

vfs_file_t *vfs_create(const char *name) {
    tmpfs_entry_t *te = tmpfs_create(name);
    if (!te) return NULL;
    vfs_file_t *f = alloc_file();
    if (!f) return NULL;
    f->src       = VFS_TMPFS;
    f->pos       = 0;
    f->size      = 0;
    f->tmpfs_ent = te;
    return f;
}

vfs_file_t *vfs_open_append(const char *name) {
    tmpfs_entry_t *te = tmpfs_find(name);
    if (!te) te = tmpfs_create(name); /* create if absent */
    if (!te) return NULL;
    vfs_file_t *f = alloc_file();
    if (!f) return NULL;
    f->src       = VFS_TMPFS;
    f->pos       = te->size; /* start at end */
    f->size      = te->size;
    f->tmpfs_ent = te;
    return f;
}

uint32_t vfs_read(vfs_file_t *f, void *buf, uint32_t len) {
    if (!f || !buf) return 0;

    if (f->src == VFS_INITRD) {
        uint32_t avail = f->size - f->pos;
        if (len > avail) len = avail;
        uint8_t *dst = (uint8_t *)buf;
        for (uint32_t i = 0; i < len; i++) dst[i] = f->initrd_data[f->pos + i];
        f->pos += len;
        return len;
    }

    /* VFS_TMPFS */
    tmpfs_entry_t *te = f->tmpfs_ent;
    uint32_t avail = te->size > f->pos ? te->size - f->pos : 0;
    if (len > avail) len = avail;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) dst[i] = te->data[f->pos + i];
    f->pos += len;
    return len;
}

uint32_t vfs_write(vfs_file_t *f, const void *buf, uint32_t len) {
    if (!f || f->src != VFS_TMPFS || !buf || !len) return 0;
    uint32_t written = tmpfs_write(f->tmpfs_ent, buf, len, f->pos);
    f->pos  += written;
    f->size  = f->tmpfs_ent->size;
    return written;
}

void vfs_close(vfs_file_t *f) {
    kfree(f);
}

const char *vfs_getent(uint32_t idx) {
    /* initrd entries come first */
    if (initrd_base) {
        const initrd_header_t *hdr = (const initrd_header_t *)initrd_base;
        const uint8_t *p   = initrd_base + sizeof(initrd_header_t);
        const uint8_t *end = initrd_base + initrd_size;
        for (uint32_t i = 0; i < hdr->count; i++) {
            if (p + sizeof(initrd_entry_t) > end) break;
            const initrd_entry_t *e = (const initrd_entry_t *)p;
            const uint8_t *data = p + sizeof(initrd_entry_t);
            if (data + e->size > end) break;
            if (i == idx) return e->name;
            p = data + e->size;
        }
        idx -= hdr->count; /* adjust for tmpfs search */
    }

    /* tmpfs entries follow the initrd entries */
    tmpfs_entry_t *te = tmpfs_get_by_idx(idx);
    return te ? te->name : NULL;
}

void vfs_list(vfs_list_cb_t cb, void *ud) {
    if (!initrd_base || !cb) return;
    const initrd_header_t *hdr = (const initrd_header_t *)initrd_base;
    const uint8_t *p   = initrd_base + sizeof(initrd_header_t);
    const uint8_t *end = initrd_base + initrd_size;
    for (uint32_t i = 0; i < hdr->count; i++) {
        if (p + sizeof(initrd_entry_t) > end) break;
        const initrd_entry_t *e = (const initrd_entry_t *)p;
        const uint8_t *data = p + sizeof(initrd_entry_t);
        if (data + e->size > end) break;
        cb(e->name, e->size, ud);
        p = data + e->size;
    }
}
