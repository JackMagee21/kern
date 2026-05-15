#include "vfs.h"
#include "tmpfs.h"
#include "initrd.h"
#include "fat16.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>

static const uint8_t *initrd_base = NULL;
static uint32_t       initrd_size = 0;

/* Current working directory — cluster 0 = root, path "/" initially. */
static uint16_t g_cwd_cluster = 0;
static char     g_cwd_path[128] = "/";

/* ── Path helpers ────────────────────────────────────────────────────────── */

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}


static void kstrncpy(char *d, const char *s, uint32_t n) {
    uint32_t i = 0;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

/*
 * Normalise a path: start from base (if rel is relative) or "/" (if absolute),
 * processing "." and ".." components.  Result is written into out[outsz].
 */
static void path_normalize(const char *base, const char *rel, char *out, uint32_t outsz) {
    uint32_t len = 0;
    out[0] = '\0';

    const char *src;
    if (rel[0] == '/') {
        out[len++] = '/'; out[len] = '\0';
        src = rel + 1;
    } else {
        while (base[len] && len < outsz - 1) { out[len] = base[len]; len++; }
        out[len] = '\0';
        src = rel;
    }

    while (*src) {
        char comp[64]; uint32_t ci = 0;
        while (*src && *src != '/') { if (ci < 63) comp[ci++] = *src; src++; }
        comp[ci] = '\0';
        if (*src == '/') src++;
        if (ci == 0 || (ci == 1 && comp[0] == '.')) continue;
        if (ci == 2 && comp[0] == '.' && comp[1] == '.') {
            while (len > 1 && out[len - 1] != '/') len--;
            if (len > 1) len--;
            out[len] = '\0';
        } else {
            if (len > 1 && len < outsz - 1) out[len++] = '/';
            for (uint32_t i = 0; i < ci && len < outsz - 1; i++)
                out[len++] = comp[i];
            out[len] = '\0';
        }
    }
    if (!out[0]) { out[0] = '/'; out[1] = '\0'; }
}

/* ── VFS init ────────────────────────────────────────────────────────────── */

void vfs_init(const void *initrd, uint32_t size) {
    if (initrd && size >= sizeof(initrd_header_t)) {
        const initrd_header_t *hdr = (const initrd_header_t *)initrd;
        if (hdr->magic == INITRD_MAGIC) {
            initrd_base = (const uint8_t *)initrd;
            initrd_size = size;
        }
    }
    tmpfs_init();
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static vfs_file_t *alloc_file(void) {
    return (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
}

/* ── CWD operations ──────────────────────────────────────────────────────── */

int vfs_chdir(const char *path) {
    if (!fat16_present()) return -1;
    uint16_t new_cluster;
    if (fat16_chdir(path, g_cwd_cluster, &new_cluster) < 0) return -1;
    g_cwd_cluster = new_cluster;
    char tmp[128];
    path_normalize(g_cwd_path, path, tmp, sizeof(tmp));
    kstrncpy(g_cwd_path, tmp, sizeof(g_cwd_path));
    return 0;
}

void vfs_getcwd(char *buf, uint32_t size) {
    uint32_t i = 0;
    while (i < size - 1 && g_cwd_path[i]) { buf[i] = g_cwd_path[i]; i++; }
    buf[i] = '\0';
}

/* ── Public API ──────────────────────────────────────────────────────────── */

vfs_file_t *vfs_open(const char *name) {
    /* 1. initrd (flat namespace only) */
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

    /* 2. FAT16 (path-aware, uses cwd) */
    if (fat16_present()) {
        uint16_t first; uint32_t sz, dlba, doff;
        if (fat16_find(name, g_cwd_cluster, &first, &sz, &dlba, &doff) == 0) {
            vfs_file_t *f = alloc_file();
            if (!f) return 0;
            f->src               = VFS_FAT;
            f->pos               = 0;
            f->size              = sz;
            f->fat.first_cluster = first;
            f->fat.dir_lba       = dlba;
            f->fat.dir_off       = doff;
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
    if (fat16_present()) {
        uint16_t first; uint32_t dlba, doff;
        if (fat16_create(name, g_cwd_cluster, &first, &dlba, &doff) == 0) {
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
    if (fat16_present()) {
        uint16_t first; uint32_t sz, dlba, doff;
        if (fat16_find(name, g_cwd_cluster, &first, &sz, &dlba, &doff) == 0) {
            vfs_file_t *f = alloc_file();
            if (!f) return 0;
            f->src               = VFS_FAT;
            f->pos               = sz;
            f->size              = sz;
            f->fat.first_cluster = first;
            f->fat.dir_lba       = dlba;
            f->fat.dir_off       = doff;
            return f;
        }
        return vfs_create(name);
    }

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

    return 0; /* initrd read-only */
}

int32_t vfs_lseek(vfs_file_t *f, int32_t offset, int whence) {
    if (!f) return -1;
    int32_t base;
    if      (whence == 0) base = 0;
    else if (whence == 1) base = (int32_t)f->pos;
    else if (whence == 2) base = (int32_t)f->size;
    else return -1;
    int32_t newpos = base + offset;
    if (newpos < 0) newpos = 0;
    f->pos = (uint32_t)newpos;
    return newpos;
}

void vfs_close(vfs_file_t *f) {
    kfree(f);
}

int vfs_unlink(const char *name) {
    if (fat16_present())
        return fat16_delete(name, g_cwd_cluster);
    /* tmpfs: no delete yet — return error */
    return -1;
}

int vfs_mkdir(const char *name) {
    if (fat16_present())
        return fat16_mkdir(name, g_cwd_cluster);
    return -1;
}

int vfs_stat(const char *name, vfs_stat_t *st) {
    if (!st) return -1;

    /* Check initrd first */
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
                st->size = e->size;
                st->type = 0;
                return 0;
            }
            p = data + e->size;
        }
    }

    /* FAT16 — check both file and dir */
    if (fat16_present()) {
        uint16_t first; uint32_t sz, dlba, doff;
        if (fat16_find(name, g_cwd_cluster, &first, &sz, &dlba, &doff) == 0) {
            st->size = sz;
            st->type = 0;
            return 0;
        }
        /* Check if it's a directory */
        uint16_t dc;
        if (fat16_chdir(name, g_cwd_cluster, &dc) == 0) {
            st->size = 0;
            st->type = 1;
            return 0;
        }
    }

    /* tmpfs */
    tmpfs_entry_t *te = tmpfs_find(name);
    if (te) {
        st->size = te->size;
        st->type = 0;
        return 0;
    }

    return -1;
}

const char *vfs_getent(uint32_t idx) {
    uint32_t base = 0;

    /* initrd entries always included */
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
        if (idx >= base) idx -= base; else return 0;
    }

    /* FAT16 entries from the current directory */
    if (fat16_present()) {
        const char *name = fat16_getent(idx, g_cwd_cluster, 0, 0);
        if (name) return name;
        uint32_t fat_count = 0;
        while (fat16_getent(fat_count, g_cwd_cluster, 0, 0)) fat_count++;
        if (idx < fat_count) return 0;
        idx -= fat_count;
    }

    /* tmpfs last (only relevant at root) */
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
