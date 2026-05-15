#include "fat16.h"
#include "ata.h"
#include "printf.h"
#include <stdint.h>
#include <stddef.h>

/* ── On-disk structures ────────────────────────────────────────────────── */

typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) bpb_t;

typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved[10];
    uint16_t wr_time;
    uint16_t wr_date;
    uint16_t first_cluster;
    uint32_t size;
} __attribute__((packed)) dirent_t;

#define ATTR_VOLUME  0x08u
#define ATTR_SUBDIR  0x10u
#define FAT_EOC      0xFFF8u
#define FAT_BAD      0xFFF7u
#define FAT_FREE     0x0000u

#define FIND_FILES  0x1
#define FIND_DIRS   0x2

/* ── Filesystem state ───────────────────────────────────────────────────── */

static int      fs_ok       = 0;
static uint32_t g_bps       = 512;
static uint32_t g_spc       = 1;
static uint32_t g_spf       = 0;
static uint32_t g_fat_lba   = 0;
static uint32_t g_root_lba  = 0;
static uint32_t g_root_secs = 0;
static uint32_t g_data_lba  = 0;
static uint32_t g_root_ents = 0;
static uint32_t g_fat_count = 0;

static uint8_t  sec[512];
static uint8_t  zerosec[512];   /* BSS-zeroed; used to wipe new clusters */

/* LBA whose data is currently in sec[]. */
static uint32_t cur_lba = 0;

/* ── Low-level sector I/O ───────────────────────────────────────────────── */

static int rsec(uint32_t lba) {
    int r = ata_read(lba, 1, sec);
    if (r < 0) kprintf("fat16: rsec(%u) FAILED\n", lba);
    cur_lba = lba;
    return r;
}
static int wsec(uint32_t lba) {
    int r = ata_write(lba, 1, sec);
    if (r < 0) kprintf("fat16: wsec(%u) FAILED\n", lba);
    return r;
}
static int wsec_cur(void) { return wsec(cur_lba); }

static uint32_t cluster_to_lba(uint16_t cluster) {
    return g_data_lba + (uint32_t)(cluster - 2u) * g_spc;
}

/* ── Directory sector I/O ───────────────────────────────────────────────── */

/*
 * Read the sec_idx-th sector of a directory into sec[], updating cur_lba.
 * dir_cluster==0  → root directory (fixed LBA range).
 * dir_cluster>=2  → subdirectory stored as a cluster chain.
 */
static int dir_rsec(uint16_t dir_cluster, uint32_t sec_idx) {
    if (dir_cluster == 0) {
        if (sec_idx >= g_root_secs) return -1;
        return rsec(g_root_lba + sec_idx);
    }
    uint32_t ci = sec_idx / g_spc;  /* cluster index along the chain */
    uint32_t si = sec_idx % g_spc;  /* sector within that cluster    */
    uint16_t cl = dir_cluster;
    for (uint32_t i = 0; i < ci; i++) {
        /* fat_get calls rsec internally, clobbering sec[]/cur_lba. */
        uint32_t byte_off = (uint32_t)cl * 2u;
        uint32_t flba = g_fat_lba + byte_off / g_bps;
        uint32_t foff = byte_off % g_bps;
        if (rsec(flba) < 0) return -1;
        uint16_t next;
        __builtin_memcpy(&next, sec + foff, 2);
        if (next < 2 || next >= FAT_EOC) return -1;
        cl = next;
    }
    return rsec(cluster_to_lba(cl) + si);
}

/* ── FAT operations ─────────────────────────────────────────────────────── */

static uint16_t fat_get(uint16_t cluster) {
    uint32_t byte_off = (uint32_t)cluster * 2u;
    uint32_t lba = g_fat_lba + byte_off / g_bps;
    uint32_t off = byte_off % g_bps;
    if (rsec(lba) < 0) return 0xFFFFu;
    uint16_t v;
    __builtin_memcpy(&v, sec + off, 2);
    return v;
}

static int fat_set(uint16_t cluster, uint16_t value) {
    uint32_t byte_off = (uint32_t)cluster * 2u;
    uint32_t rel_sec  = byte_off / g_bps;
    uint32_t off      = byte_off % g_bps;
    for (uint32_t f = 0; f < g_fat_count; f++) {
        uint32_t lba = g_fat_lba + f * g_spf + rel_sec;
        if (rsec(lba) < 0) return -1;
        __builtin_memcpy(sec + off, &value, 2);
        if (wsec(lba) < 0) return -1;
    }
    return 0;
}

static uint16_t fat_alloc(void) {
    uint32_t max_cluster = (g_spf * g_bps) / 2u;
    if (max_cluster > 0xFFF0u) max_cluster = 0xFFF0u;
    for (uint16_t c = 2; c < (uint16_t)max_cluster; c++) {
        if (fat_get(c) == FAT_FREE) {
            fat_set(c, 0xFFFFu);
            uint32_t base = cluster_to_lba(c);
            for (uint32_t s = 0; s < g_spc; s++)
                ata_write(base + s, 1, zerosec);
            return c;
        }
    }
    return 0;
}

static void fat_free_chain(uint16_t cluster) {
    while (cluster >= 2u && cluster < FAT_EOC && cluster != FAT_BAD) {
        uint16_t next = fat_get(cluster);
        fat_set(cluster, FAT_FREE);
        cluster = next;
    }
}

static uint16_t chain_walk(uint16_t *first_ptr, uint32_t pos, int alloc) {
    uint32_t cluster_bytes = g_spc * g_bps;
    uint32_t target_idx    = pos / cluster_bytes;
    uint16_t cluster = *first_ptr;
    uint16_t prev    = 0;
    for (uint32_t i = 0; i <= target_idx; i++) {
        if (cluster < 2u || cluster >= FAT_EOC) {
            if (!alloc) return 0;
            uint16_t nc = fat_alloc();
            if (!nc) return 0;
            if (prev) fat_set(prev, nc);
            else      *first_ptr = nc;
            cluster = nc;
        }
        if (i == target_idx) return cluster;
        prev    = cluster;
        cluster = fat_get(cluster);
    }
    return 0;
}

/* ── Name helpers ───────────────────────────────────────────────────────── */

static void to_83(const char *name, char out[11]) {
    int i;
    for (i = 0; i < 11; i++) out[i] = ' ';
    /* Special-case "." and ".." */
    if (name[0] == '.' && name[1] == '\0') { out[0] = '.'; return; }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        out[0] = '.'; out[1] = '.'; return;
    }
    i = 0;
    while (*name && *name != '.' && i < 8) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[i++] = c;
    }
    if (*name == '.') {
        name++;
        int j = 8;
        while (*name && j < 11) {
            char c = *name++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out[j++] = c;
        }
    }
}

static int cmp83(const char *a, const char *b, int len) {
    for (int i = 0; i < len; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return 0;
    }
    return 1;
}

static void from_83(const char n[8], const char e[3], char out[13]) {
    int j = 0, i;
    for (i = 0; i < 8 && n[i] != ' '; i++) out[j++] = n[i];
    int has_ext = 0;
    for (i = 0; i < 3; i++) if (e[i] != ' ') { has_ext = 1; break; }
    if (has_ext) {
        out[j++] = '.';
        for (i = 0; i < 3 && e[i] != ' '; i++) out[j++] = e[i];
    }
    out[j] = '\0';
}

/* ── Directory search helpers ───────────────────────────────────────────── */

/*
 * Search dir_cluster for a dirent whose 8.3 name matches name83[11].
 * flags: FIND_FILES (skip subdirs), FIND_DIRS (skip files), or both.
 * out_* parameters are optional (may be NULL).
 */
static int find_in(uint16_t dir_cluster, const char name83[11], int flags,
                   uint16_t *out_first, uint32_t *out_size,
                   uint32_t *out_lba, uint32_t *out_off) {
    uint32_t eps = g_bps / 32u;
    for (uint32_t s = 0; ; s++) {
        if (dir_rsec(dir_cluster, s) < 0) break;
        for (uint32_t i = 0; i < eps; i++) {
            dirent_t *d = (dirent_t *)(sec + i * 32u);
            uint8_t first = (uint8_t)d->name[0];
            if (first == 0x00) return -1;
            if (first == 0xE5) continue;
            if (d->attr & ATTR_VOLUME) continue;
            int is_dir = (d->attr & ATTR_SUBDIR) ? 1 : 0;
            if ( is_dir && !(flags & FIND_DIRS))  continue;
            if (!is_dir && !(flags & FIND_FILES)) continue;
            if (cmp83(d->name, name83, 8) && cmp83(d->ext, name83 + 8, 3)) {
                if (out_first) *out_first = d->first_cluster;
                if (out_size)  *out_size  = d->size;
                if (out_lba)   *out_lba   = cur_lba;
                if (out_off)   *out_off   = i * 32u;
                return 0;
            }
        }
    }
    return -1;
}

/* Look up a subdirectory by plain name in dir_cluster. */
static int find_subdir(uint16_t dir_cluster, const char *name, uint16_t *cluster_out) {
    char name83[11];
    to_83(name, name83);
    return find_in(dir_cluster, name83, FIND_DIRS, cluster_out, NULL, NULL, NULL);
}

/*
 * Resolve a path (with optional '/' separators) to the parent directory
 * cluster and the basename component.
 *
 * Absolute paths ("/…") start from root (cluster 0).
 * Relative paths start from cwd_cluster.
 * basename_out points into a static buffer — use it before the next call.
 */
static int resolve_fat_path(const char *path, uint16_t cwd_cluster,
                             uint16_t *dir_cluster_out, const char **basename_out) {
    static char buf[256];
    uint32_t i;
    for (i = 0; path[i] && i < 255; i++) buf[i] = path[i];
    buf[i] = '\0';

    char *p = buf;
    uint16_t dir = (*p == '/') ? 0 : cwd_cluster;
    if (*p == '/') while (*p == '/') p++;

    /* Walk all slash-separated components; stop at the last one (basename). */
    char *basename = p;
    for (;;) {
        char *slash = p;
        while (*slash && *slash != '/') slash++;
        if (*slash == '\0') {   /* last component = basename */
            basename = p;
            break;
        }
        *slash = '\0';
        if (p[0] != '\0') {
            if (p[0] == '.' && p[1] == '\0') {
                /* "." — stay */
            } else if (p[0] == '.' && p[1] == '.' && p[2] == '\0') {
                /* ".." — jump to parent via the ".." dirent */
                if (dir != 0) {
                    if (dir_rsec(dir, 0) >= 0) {
                        dirent_t *dd = (dirent_t *)(sec + 32u);
                        dir = dd->first_cluster;
                    }
                }
            } else {
                uint16_t next;
                if (find_subdir(dir, p, &next) < 0) return -1;
                dir = next;
            }
        }
        p = slash + 1;
        while (*p == '/') p++;
    }
    *dir_cluster_out = dir;
    *basename_out    = basename;
    return 0;
}

/* ── Directory write helpers ────────────────────────────────────────────── */

static int update_dirent(uint32_t dir_lba, uint32_t dir_off,
                          uint16_t first_cluster, uint32_t size) {
    if (rsec(dir_lba) < 0) return -1;
    dirent_t *d = (dirent_t *)(sec + dir_off);
    d->first_cluster = first_cluster;
    d->size          = size;
    return wsec_cur();
}

/*
 * Find (or allocate) a free dirent slot in dir_cluster.
 * For subdirectories the chain is extended if no free slot exists.
 * Root directories are fixed-size — returns -1 if full.
 */
static int alloc_dirent(uint16_t dir_cluster,
                        uint32_t *lba_out, uint32_t *off_out) {
    uint32_t eps = g_bps / 32u;
    uint32_t total_secs = 0;
    int found = 0;

    for (uint32_t s = 0; !found; s++) {
        if (dir_cluster == 0 && s >= g_root_secs) break;
        if (dir_rsec(dir_cluster, s) < 0) break;
        total_secs = s + 1;

        for (uint32_t i = 0; i < eps; i++) {
            uint8_t first = (uint8_t)sec[i * 32u];
            if (first == 0x00 || first == 0xE5) {
                *lba_out = cur_lba;
                *off_out = i * 32u;
                found = 1;
                break;
            }
        }
    }
    if (found) return 0;
    if (dir_cluster == 0) return -1;   /* root dir full */

    /* Extend subdirectory chain. */
    uint16_t dc = dir_cluster;
    uint16_t new_cl = chain_walk(&dc, total_secs * g_bps, 1);
    if (!new_cl) return -1;
    *lba_out = cluster_to_lba(new_cl);
    *off_out = 0;
    return 0;
}

/* Internal: create/truncate a file entry inside a known directory. */
static int create_in(uint16_t dir_cluster, const char *name,
                     uint16_t *first_cluster_out,
                     uint32_t *dir_lba_out, uint32_t *dir_off_out) {
    char name83[11];
    to_83(name, name83);

    /* Check for existing file → truncate. */
    uint16_t ex_first; uint32_t ex_lba, ex_off;
    if (find_in(dir_cluster, name83, FIND_FILES,
                &ex_first, NULL, &ex_lba, &ex_off) == 0) {
        fat_free_chain(ex_first);
        if (rsec(ex_lba) < 0) return -1;
        dirent_t *d = (dirent_t *)(sec + ex_off);
        d->first_cluster = 0;
        d->size          = 0;
        if (wsec_cur() < 0) return -1;
        *first_cluster_out = 0;
        *dir_lba_out = ex_lba;
        *dir_off_out = ex_off;
        return 0;
    }

    /* Allocate a free dirent slot. */
    uint32_t free_lba, free_off;
    if (alloc_dirent(dir_cluster, &free_lba, &free_off) < 0) return -1;

    if (rsec(free_lba) < 0) return -1;
    dirent_t *d = (dirent_t *)(sec + free_off);
    for (int i = 0; i < 8;  i++) d->name[i] = name83[i];
    for (int i = 0; i < 3;  i++) d->ext[i]  = name83[8 + i];
    d->attr          = 0x20u;
    for (int i = 0; i < 10; i++) d->reserved[i] = 0;
    d->wr_time       = 0;
    d->wr_date       = 0;
    d->first_cluster = 0;
    d->size          = 0;
    if (wsec_cur() < 0) return -1;

    *first_cluster_out = 0;
    *dir_lba_out = free_lba;
    *dir_off_out = free_off;
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int fat16_init(void) {
    if (!ata_present()) return -1;
    if (rsec(0) < 0) return -1;

    bpb_t *bpb = (bpb_t *)sec;
    if (bpb->bytes_per_sector != 512) return -1;
    if (!bpb->sectors_per_cluster)    return -1;

    g_bps       = bpb->bytes_per_sector;
    g_spc       = bpb->sectors_per_cluster;
    g_spf       = bpb->sectors_per_fat;
    g_fat_count = bpb->fat_count;
    g_root_ents = bpb->root_entry_count;

    g_fat_lba   = bpb->reserved_sectors;
    g_root_lba  = g_fat_lba + (uint32_t)bpb->fat_count * g_spf;
    g_root_secs = (g_root_ents * 32u + g_bps - 1u) / g_bps;
    g_data_lba  = g_root_lba + g_root_secs;

    fs_ok = 1;
    return 0;
}

int fat16_present(void) { return fs_ok; }

int fat16_find(const char *path, uint16_t cwd_cluster,
               uint16_t *first_cluster, uint32_t *size,
               uint32_t *dir_lba, uint32_t *dir_off) {
    if (!fs_ok) return -1;
    uint16_t dir; const char *base;
    if (resolve_fat_path(path, cwd_cluster, &dir, &base) < 0) return -1;
    if (!base || !base[0]) return -1;
    char name83[11];
    to_83(base, name83);
    return find_in(dir, name83, FIND_FILES, first_cluster, size, dir_lba, dir_off);
}

int fat16_create(const char *path, uint16_t cwd_cluster,
                 uint16_t *first_cluster, uint32_t *dir_lba, uint32_t *dir_off) {
    if (!fs_ok) return -1;
    uint16_t dir; const char *base;
    if (resolve_fat_path(path, cwd_cluster, &dir, &base) < 0) return -1;
    if (!base || !base[0]) return -1;
    return create_in(dir, base, first_cluster, dir_lba, dir_off);
}

int fat16_delete(const char *path, uint16_t cwd_cluster) {
    if (!fs_ok) return -1;
    uint16_t dir; const char *base;
    if (resolve_fat_path(path, cwd_cluster, &dir, &base) < 0) return -1;
    if (!base || !base[0]) return -1;

    char name83[11];
    to_83(base, name83);
    uint16_t first; uint32_t lba, off;
    if (find_in(dir, name83, FIND_FILES, &first, NULL, &lba, &off) < 0) return -1;

    fat_free_chain(first);

    if (rsec(lba) < 0) return -1;
    sec[off] = 0xE5u;
    return wsec_cur();
}

int fat16_mkdir(const char *path, uint16_t cwd_cluster) {
    if (!fs_ok) return -1;
    uint16_t parent; const char *base;
    if (resolve_fat_path(path, cwd_cluster, &parent, &base) < 0) return -1;
    if (!base || !base[0]) return -1;

    char name83[11];
    to_83(base, name83);
    /* Refuse if it already exists. */
    if (find_in(parent, name83, FIND_DIRS, NULL, NULL, NULL, NULL) == 0)
        return -1;

    uint16_t new_cl = fat_alloc();
    if (!new_cl) return -1;

    /* Write "." and ".." into the new cluster. */
    uint32_t new_lba = cluster_to_lba(new_cl);
    if (rsec(new_lba) < 0) { fat_set(new_cl, FAT_FREE); return -1; }

    dirent_t *dot = (dirent_t *)(sec);
    dot->name[0] = '.';
    for (int i = 1; i < 8; i++) dot->name[i] = ' ';
    for (int i = 0; i < 3; i++) dot->ext[i]  = ' ';
    dot->attr = ATTR_SUBDIR;
    for (int i = 0; i < 10; i++) dot->reserved[i] = 0;
    dot->wr_time = dot->wr_date = 0;
    dot->first_cluster = new_cl;
    dot->size = 0;

    dirent_t *dotdot = (dirent_t *)(sec + 32u);
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    for (int i = 2; i < 8; i++) dotdot->name[i] = ' ';
    for (int i = 0; i < 3; i++) dotdot->ext[i]  = ' ';
    dotdot->attr = ATTR_SUBDIR;
    for (int i = 0; i < 10; i++) dotdot->reserved[i] = 0;
    dotdot->wr_time = dotdot->wr_date = 0;
    dotdot->first_cluster = parent;   /* 0 when parent is root */
    dotdot->size = 0;

    if (wsec_cur() < 0) { fat_set(new_cl, FAT_FREE); return -1; }

    /* Add the subdir entry to the parent. */
    uint32_t dent_lba, dent_off;
    if (alloc_dirent(parent, &dent_lba, &dent_off) < 0) {
        fat_set(new_cl, FAT_FREE); return -1;
    }
    if (rsec(dent_lba) < 0) return -1;
    dirent_t *d = (dirent_t *)(sec + dent_off);
    for (int i = 0; i < 8; i++) d->name[i] = name83[i];
    for (int i = 0; i < 3; i++) d->ext[i]  = name83[8 + i];
    d->attr          = ATTR_SUBDIR;
    for (int i = 0; i < 10; i++) d->reserved[i] = 0;
    d->wr_time = d->wr_date = 0;
    d->first_cluster = new_cl;
    d->size          = 0;
    return wsec_cur();
}

int fat16_chdir(const char *path, uint16_t cwd_cluster, uint16_t *new_cluster_out) {
    if (!fs_ok) return -1;

    /* Special case: cd / */
    if (path[0] == '/' && path[1] == '\0') { *new_cluster_out = 0; return 0; }

    uint16_t dir; const char *base;
    if (resolve_fat_path(path, cwd_cluster, &dir, &base) < 0) return -1;

    /* Path ended with '/' or resolved to just a directory. */
    if (!base || base[0] == '\0') { *new_cluster_out = dir; return 0; }

    /* "." */
    if (base[0] == '.' && base[1] == '\0') { *new_cluster_out = dir; return 0; }

    /* ".." — read the ".." entry from the current dir. */
    if (base[0] == '.' && base[1] == '.' && base[2] == '\0') {
        if (dir == 0) { *new_cluster_out = 0; return 0; }
        if (dir_rsec(dir, 0) >= 0) {
            dirent_t *dd = (dirent_t *)(sec + 32u);
            *new_cluster_out = dd->first_cluster;
            return 0;
        }
        return -1;
    }

    uint16_t next;
    if (find_subdir(dir, base, &next) < 0) return -1;
    *new_cluster_out = next;
    return 0;
}

uint32_t fat16_read(uint16_t first_cluster, uint32_t pos, void *buf, uint32_t len) {
    if (!fs_ok || !len || first_cluster < 2u) return 0;

    uint32_t cluster_bytes = g_spc * g_bps;
    uint8_t *dst  = (uint8_t *)buf;
    uint32_t done = 0;

    while (done < len) {
        uint16_t cluster = chain_walk(&first_cluster, pos + done, 0);
        if (!cluster) break;

        uint32_t cluster_off      = (pos + done) % cluster_bytes;
        uint32_t remain_in_cl     = cluster_bytes - cluster_off;
        uint32_t to_read          = len - done;
        if (to_read > remain_in_cl) to_read = remain_in_cl;

        uint32_t lba_base         = cluster_to_lba(cluster);
        uint32_t read_in_cluster  = 0;
        while (read_in_cluster < to_read) {
            uint32_t sec_idx = (cluster_off + read_in_cluster) / g_bps;
            uint32_t sec_off = (cluster_off + read_in_cluster) % g_bps;
            uint32_t avail   = g_bps - sec_off;
            uint32_t chunk   = to_read - read_in_cluster;
            if (chunk > avail) chunk = avail;
            if (rsec(lba_base + sec_idx) < 0) return done;
            for (uint32_t k = 0; k < chunk; k++)
                dst[done + read_in_cluster + k] = sec[sec_off + k];
            read_in_cluster += chunk;
        }
        done += to_read;
    }
    return done;
}

uint32_t fat16_write(uint16_t *first_cluster_ptr,
                     uint32_t dir_lba, uint32_t dir_off,
                     uint32_t pos, const void *buf, uint32_t len,
                     uint32_t *file_size) {
    if (!fs_ok || !len) return 0;

    uint32_t       cluster_bytes = g_spc * g_bps;
    const uint8_t *src  = (const uint8_t *)buf;
    uint32_t       done = 0;
    int            cluster_changed = 0;

    while (done < len) {
        uint16_t cluster = chain_walk(first_cluster_ptr, pos + done, 1);
        if (!cluster) break;
        cluster_changed = 1;

        uint32_t cluster_off = (pos + done) % cluster_bytes;
        uint32_t remain      = cluster_bytes - cluster_off;
        uint32_t to_write    = len - done;
        if (to_write > remain) to_write = remain;

        uint32_t lba_base    = cluster_to_lba(cluster);
        uint32_t written_in  = 0;
        while (written_in < to_write) {
            uint32_t sec_idx = (cluster_off + written_in) / g_bps;
            uint32_t sec_off = (cluster_off + written_in) % g_bps;
            uint32_t avail   = g_bps - sec_off;
            uint32_t chunk   = to_write - written_in;
            if (chunk > avail) chunk = avail;
            if (sec_off != 0 || chunk < g_bps) {
                if (rsec(lba_base + sec_idx) < 0) return done;
            }
            for (uint32_t k = 0; k < chunk; k++)
                sec[sec_off + k] = src[done + written_in + k];
            if (wsec(lba_base + sec_idx) < 0) return done;
            written_in += chunk;
        }
        done += to_write;
    }

    uint32_t new_end = pos + done;
    if (new_end > *file_size) *file_size = new_end;
    if (cluster_changed || new_end > pos)
        update_dirent(dir_lba, dir_off, *first_cluster_ptr, *file_size);

    return done;
}

/*
 * Return the idx-th non-deleted, non-volume entry in dir_cluster.
 * Skips "." and ".." entries.
 * Sets *size_out (may be NULL) and *is_dir_out (may be NULL).
 */
const char *fat16_getent(uint32_t idx, uint16_t dir_cluster,
                          uint32_t *size_out, int *is_dir_out) {
    if (!fs_ok) return 0;
    static char name_buf[13];
    uint32_t eps   = g_bps / 32u;
    uint32_t count = 0;

    for (uint32_t s = 0; ; s++) {
        if (dir_rsec(dir_cluster, s) < 0) return 0;
        for (uint32_t i = 0; i < eps; i++) {
            dirent_t *d    = (dirent_t *)(sec + i * 32u);
            uint8_t   first = (uint8_t)d->name[0];
            if (first == 0x00) return 0;
            if (first == 0xE5) continue;
            if (d->attr & ATTR_VOLUME) continue;
            if (first == '.') continue;        /* skip "." and ".." */
            if (count == idx) {
                from_83(d->name, d->ext, name_buf);
                if (size_out)   *size_out   = d->size;
                if (is_dir_out) *is_dir_out = (d->attr & ATTR_SUBDIR) ? 1 : 0;
                return name_buf;
            }
            count++;
        }
    }
    return 0;
}
