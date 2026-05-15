#include "vfs.h"
#include "tmpfs.h"
#include "initrd.h"
#include "fat16.h"
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
    if (initrd && size >= sizeof(initrd_header_t)) {
        const initrd_header_t *hdr = (const initrd_header_t *)initrd;
        if (hdr->magic == INITRD_MAGIC) {
            initrd_base = (const uint8_t *)initrd;
            initrd_size = size;
        }
    }
    tmpfs_init();
    /* FAT16 is initialised separately by kernel_main after ata_init(). */
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

static vfs_file_t *alloc_file(void) {
    return (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
}

/* ── Public API ─────────────────────────────────────────────────────────── */

vfs_file_t *vfs_open(const char *name) {
    /* 1. initrd */
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
                if (!f) return 0;
                f->src         = VFS_INITRD;
                f->pos         = 0;
                f->size        = e->size;
                f->initrd_data = data;
                return f;
            }
            p = data + e->size;
        }
    }

    /* 2. FAT16 */
    if (fat16_present()) {
        uint16_t first; uint32_t sz, dlba, doff;
        if (fat16_find(name, &first, &sz, &dlba, &doff) == 0) {
            vfs_file_t *f = alloc_file();
            if (!f) return 0;
            f->src             = VFS_FAT;
            f->pos             = 0;
            f->size            = sz;
            f->fat.first_cluster = first;
            f->fat.dir_lba     = dlba;
            f->fat.dir_off     = doff;
            return f;
        }
    }

    /* 3. tmpfs */
    tmpfs_entry_t *te = tmpfs_find(name);
    if (te) {
        vfs_file_t *f = alloc_file();
        if (!f) return 0;
        f->src       = VFS_TMPFS;
        f->pos       = 0;
        f->size      = te->size;
        f->tmpfs_ent = te;
        return f;
    }

    return 0;
}

vfs_file_t *vfs_create(const char *name) {
    /* Prefer FAT16 for persistence. */
    if (fat16_present()) {
        uint16_t first; uint32_t dlba, doff;
        if (fat16_create(name, &first, &dlba, &doff) == 0) {
            vfs_file_t *f = alloc_file();
            if (!f) return 0;
            f->src               = VFS_FAT;
            f->pos               = 0;
            f->size              = 0;
            f->fat.first_cluster = first;
            f->fat.dir_lba       = dlba;
            f->fat.dir_off       = doff;
            return f;
        }
    }

    /* Fall back to tmpfs. */
    tmpfs_entry_t *te = tmpfs_create(name);
    if (!te) return 0;
    vfs_file_t *f = alloc_file();
    if (!f) return 0;
    f->src       = VFS_TMPFS;
    f->pos       = 0;
    f->size      = 0;
    f->tmpfs_ent = te;
    return f;
}

vfs_file_t *vfs_open_append(const char *name) {
    /* Prefer FAT16 for persistence. */
    if (fat16_present()) {
        uint16_t first; uint32_t sz, dlba, doff;
        if (fat16_find(name, &first, &sz, &dlba, &doff) == 0) {
            vfs_file_t *f = alloc_file();
            if (!f) return 0;
            f->src               = VFS_FAT;
            f->pos               = sz;   /* start at end */
            f->size              = sz;
            f->fat.first_cluster = first;
            f->fat.dir_lba       = dlba;
            f->fat.dir_off       = doff;
            return f;
        }
        /* File not found on FAT — create it. */
        return vfs_create(name);
    }

    /* tmpfs fallback. */
    tmpfs_entry_t *te = tmpfs_find(name);
    if (!te) te = tmpfs_create(name);
    if (!te) return 0;
    vfs_file_t *f = alloc_file();
    if (!f) return 0;
    f->src       = VFS_TMPFS;
    f->pos       = te->size;
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

    if (f->src == VFS_FAT) {
        uint32_t avail = f->size > f->pos ? f->size - f->pos : 0;
        if (len > avail) len = avail;
        if (!len) return 0;
        uint32_t n = fat16_read(f->fat.first_cluster, f->pos, buf, len);
        f->pos += n;
        return n;
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
    if (!f || !buf || !len) return 0;

    if (f->src == VFS_FAT) {
        uint32_t n = fat16_write(&f->fat.first_cluster,
                                  f->fat.dir_lba, f->fat.dir_off,
                                  f->pos, buf, len, &f->size);
        f->pos += n;
        return n;
    }

    if (f->src == VFS_TMPFS) {
        uint32_t written = tmpfs_write(f->tmpfs_ent, buf, len, f->pos);
        f->pos  += written;
        f->size  = f->tmpfs_ent->size;
        return written;
    }

    return 0; /* initrd is read-only */
}

void vfs_close(vfs_file_t *f) {
    kfree(f);
}

const char *vfs_getent(uint32_t idx) {
    uint32_t base = 0;

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
        base = hdr->count;
        idx -= (idx < base) ? idx : base;
    }

    /* FAT16 entries next */
    if (fat16_present()) {
        const char *name = fat16_getent(idx, 0);
        if (name) return name;
        /* Count FAT16 entries to advance idx for tmpfs. */
        uint32_t fat_count = 0;
        while (fat16_getent(fat_count, 0)) fat_count++;
        if (idx < fat_count) return 0;
        idx -= fat_count;
    }

    /* tmpfs last */
    tmpfs_entry_t *te = tmpfs_get_by_idx(idx);
    return te ? te->name : 0;
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
