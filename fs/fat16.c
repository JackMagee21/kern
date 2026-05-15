#include "fat16.h"
#include "ata.h"
#include "printf.h"
#include <stdint.h>

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

/* ── Filesystem state ───────────────────────────────────────────────────── */

static int      fs_ok        = 0;
static uint32_t g_bps        = 512;   /* bytes per sector   */
static uint32_t g_spc        = 1;     /* sectors per cluster */
static uint32_t g_spf        = 0;     /* sectors per FAT    */
static uint32_t g_fat_lba    = 0;     /* LBA of FAT1        */
static uint32_t g_root_lba   = 0;     /* LBA of root dir    */
static uint32_t g_root_secs  = 0;     /* sectors in root dir */
static uint32_t g_data_lba   = 0;     /* LBA of data area   */
static uint32_t g_root_ents  = 0;     /* max root dir entries */
static uint32_t g_fat_count  = 0;

/* Single working sector buffer — all I/O goes through here. */
static uint8_t  sec[512];

/* Static zero-filled buffer for zeroing freshly allocated clusters. */
static uint8_t  zerosec[512];   /* BSS zero init — no explicit zeroing needed */

/* ── Low-level helpers ──────────────────────────────────────────────────── */

static int rsec(uint32_t lba) {
    int r = ata_read(lba, 1, sec);
    if (r < 0) kprintf("fat16: rsec(%u) FAILED\n", lba);
    return r;
}
static int wsec(uint32_t lba) {
    int r = ata_write(lba, 1, sec);
    if (r < 0) kprintf("fat16: wsec(%u) FAILED\n", lba);
    return r;
}

static uint32_t cluster_to_lba(uint16_t cluster) {
    return g_data_lba + (uint32_t)(cluster - 2u) * g_spc;
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
    /* Update both FAT copies. */
    for (uint32_t f = 0; f < g_fat_count; f++) {
        uint32_t lba = g_fat_lba + f * g_spf + rel_sec;
        if (rsec(lba) < 0) return -1;
        __builtin_memcpy(sec + off, &value, 2);
        if (wsec(lba) < 0) return -1;
    }
    return 0;
}

/* Allocate a free cluster: marks it EOC in FAT and zeroes its sectors. */
static uint16_t fat_alloc(void) {
    kprintf("fat16: fat_alloc scanning...\n");
    uint32_t max_cluster = (g_spf * g_bps) / 2u;
    if (max_cluster > 0xFFF0u) max_cluster = 0xFFF0u;

    for (uint16_t c = 2; c < (uint16_t)max_cluster; c++) {
        if (fat_get(c) == FAT_FREE) {
            fat_set(c, 0xFFFFu);
            /* Zero every sector of this cluster. */
            uint32_t base = cluster_to_lba(c);
            for (uint32_t s = 0; s < g_spc; s++)
                ata_write(base + s, 1, zerosec);
            return c;
        }
    }
    return 0; /* disk full */
}

/* Free an entire cluster chain. */
static void fat_free_chain(uint16_t cluster) {
    while (cluster >= 2u && cluster < FAT_EOC && cluster != FAT_BAD) {
        uint16_t next = fat_get(cluster);
        fat_set(cluster, FAT_FREE);
        cluster = next;
    }
}

/*
 * Walk the cluster chain from first_cluster to the cluster that contains
 * byte offset pos.  If the chain is shorter than needed and alloc==1,
 * new clusters are appended; *first_ptr is updated if the file was empty.
 * Returns 0 on failure/EOF.
 */
static uint16_t chain_walk(uint16_t *first_ptr, uint32_t pos, int alloc) {
    uint32_t cluster_bytes = g_spc * g_bps;
    uint32_t target_idx    = pos / cluster_bytes;

    uint16_t cluster = *first_ptr;
    uint16_t prev    = 0;

    for (uint32_t i = 0; i <= target_idx; i++) {
        if (cluster < 2u || cluster >= FAT_EOC) {
            /* No more clusters — allocate if requested. */
            if (!alloc) return 0;
            uint16_t nc = fat_alloc();
            if (!nc) return 0;
            if (prev) fat_set(prev, nc);   /* link previous → new */
            else      *first_ptr = nc;     /* first cluster of file */
            cluster = nc;
        }
        if (i == target_idx) return cluster;
        prev    = cluster;
        cluster = fat_get(cluster);
    }
    return 0; /* unreachable but keeps gcc happy */
}

/* ── Name helpers ───────────────────────────────────────────────────────── */

/* Convert any filename to the 11-byte 8.3 space-padded uppercase form. */
static void to_83(const char *name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0;
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

/* Convert 8.3 dir name+ext to a printable "name.ext\0" string. */
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

/* ── Directory helpers ──────────────────────────────────────────────────── */

/*
 * Update the first_cluster and size fields of a directory entry in place.
 * Reads the sector, modifies the dirent at byte offset dir_off, writes back.
 */
static int update_dirent(uint32_t dir_lba, uint32_t dir_off,
                          uint16_t first_cluster, uint32_t size) {
    kprintf("fat16: update_dirent lba=%u off=%u clus=%u sz=%u\n",
            dir_lba, dir_off, first_cluster, size);
    if (rsec(dir_lba) < 0) return -1;
    dirent_t *d = (dirent_t *)(sec + dir_off);
    d->first_cluster = first_cluster;
    d->size          = size;
    int r = wsec(dir_lba);
    kprintf("fat16: update_dirent wsec=%d\n", r);
    return r;
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

int fat16_find(const char *name,
               uint16_t *first_cluster, uint32_t *size,
               uint32_t *dir_lba, uint32_t *dir_off) {
    if (!fs_ok) return -1;

    char name83[11];
    to_83(name, name83);

    uint32_t eps = g_bps / 32u;  /* directory entries per sector */

    for (uint32_t s = 0; s < g_root_secs; s++) {
        uint32_t lba = g_root_lba + s;
        if (rsec(lba) < 0) return -1;

        for (uint32_t i = 0; i < eps; i++) {
            dirent_t *d = (dirent_t *)(sec + i * 32u);
            uint8_t first = (uint8_t)d->name[0];

            if (first == 0x00) return -1;  /* end of directory */
            if (first == 0xE5) continue;   /* deleted entry     */
            if (d->attr & (ATTR_VOLUME | ATTR_SUBDIR)) continue;

            if (cmp83(d->name, name83, 8) && cmp83(d->ext, name83 + 8, 3)) {
                *first_cluster = d->first_cluster;
                *size          = d->size;
                *dir_lba       = lba;
                *dir_off       = i * 32u;
                kprintf("fat16: find ok clus=%u size=%u\n", *first_cluster, *size);
                return 0;
            }
        }
    }
    return -1;
}

int fat16_create(const char *name,
                 uint16_t *first_cluster,
                 uint32_t *dir_lba, uint32_t *dir_off) {
    if (!fs_ok) return -1;

    char name83[11];
    to_83(name, name83);

    uint32_t eps        = g_bps / 32u;
    uint32_t free_lba   = 0;
    uint32_t free_off   = 0;
    int      found_free = 0;

    /* First pass: look for existing entry (truncate) OR a free slot. */
    for (uint32_t s = 0; s < g_root_secs; s++) {
        uint32_t lba = g_root_lba + s;
        if (rsec(lba) < 0) return -1;

        for (uint32_t i = 0; i < eps; i++) {
            dirent_t *d = (dirent_t *)(sec + i * 32u);
            uint8_t first = (uint8_t)d->name[0];

            if (first == 0x00 || first == 0xE5) {
                if (!found_free) {
                    free_lba   = lba;
                    free_off   = i * 32u;
                    found_free = 1;
                }
                if (first == 0x00) goto done_scan;  /* no more entries */
                continue;
            }
            if (d->attr & (ATTR_VOLUME | ATTR_SUBDIR)) continue;

            if (cmp83(d->name, name83, 8) && cmp83(d->ext, name83 + 8, 3)) {
                /* Existing entry — truncate it. */
                fat_free_chain(d->first_cluster);
                d->first_cluster = 0;
                d->size          = 0;
                if (wsec(lba) < 0) return -1;
                *first_cluster = 0;
                *dir_lba = lba;
                *dir_off = i * 32u;
                return 0;
            }
        }
    }
done_scan:
    if (!found_free) return -1;  /* root directory is full */

    /* Write a new directory entry in the free slot. */
    if (rsec(free_lba) < 0) return -1;
    dirent_t *d = (dirent_t *)(sec + free_off);
    for (int i = 0; i < 8;  i++) d->name[i] = name83[i];
    for (int i = 0; i < 3;  i++) d->ext[i]  = name83[8 + i];
    d->attr          = 0x20u;  /* archive */
    for (int i = 0; i < 10; i++) d->reserved[i] = 0;
    d->wr_time       = 0;
    d->wr_date       = 0;
    d->first_cluster = 0;
    d->size          = 0;
    if (wsec(free_lba) < 0) return -1;

    *first_cluster = 0;
    *dir_lba       = free_lba;
    *dir_off       = free_off;
    kprintf("fat16: create ok lba=%u off=%u\n", free_lba, free_off);
    return 0;
}

uint32_t fat16_read(uint16_t first_cluster, uint32_t pos,
                    void *buf, uint32_t len) {
    if (!fs_ok || !len || first_cluster < 2u) return 0;

    uint32_t cluster_bytes = g_spc * g_bps;
    uint8_t *dst    = (uint8_t *)buf;
    uint32_t done   = 0;

    while (done < len) {
        uint16_t cluster = chain_walk(&first_cluster, pos + done, 0);
        if (!cluster) break;

        uint32_t cluster_off = (pos + done) % cluster_bytes;
        uint32_t remain_in_cluster = cluster_bytes - cluster_off;
        uint32_t to_read = len - done;
        if (to_read > remain_in_cluster) to_read = remain_in_cluster;

        /* Read sector by sector within the cluster. */
        uint32_t lba_base = cluster_to_lba(cluster);
        uint32_t read_in_cluster = 0;

        while (read_in_cluster < to_read) {
            uint32_t sec_idx = (cluster_off + read_in_cluster) / g_bps;
            uint32_t sec_off = (cluster_off + read_in_cluster) % g_bps;
            uint32_t avail   = g_bps - sec_off;
            uint32_t chunk   = to_read - read_in_cluster;
            if (chunk > avail) chunk = avail;

            if (rsec(lba_base + sec_idx) < 0) return done;
            for (uint32_t i = 0; i < chunk; i++)
                dst[done + read_in_cluster + i] = sec[sec_off + i];

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
    kprintf("fat16: write pos=%u len=%u clus=%u\n", pos, len, *first_cluster_ptr);
    if (!fs_ok || !len) return 0;

    uint32_t cluster_bytes = g_spc * g_bps;
    const uint8_t *src  = (const uint8_t *)buf;
    uint32_t done       = 0;
    int      cluster_changed = 0;

    while (done < len) {
        uint16_t cluster = chain_walk(first_cluster_ptr, pos + done, 1);
        if (!cluster) break;

        /* If chain_walk had to update first_cluster, flag for dirent update. */
        cluster_changed = 1;

        uint32_t cluster_off = (pos + done) % cluster_bytes;
        uint32_t remain      = cluster_bytes - cluster_off;
        uint32_t to_write    = len - done;
        if (to_write > remain) to_write = remain;

        uint32_t lba_base   = cluster_to_lba(cluster);
        uint32_t written_in = 0;

        while (written_in < to_write) {
            uint32_t sec_idx = (cluster_off + written_in) / g_bps;
            uint32_t sec_off = (cluster_off + written_in) % g_bps;
            uint32_t avail   = g_bps - sec_off;
            uint32_t chunk   = to_write - written_in;
            if (chunk > avail) chunk = avail;

            /* Read-modify-write for partial sector writes. */
            if (sec_off != 0 || chunk < g_bps) {
                if (rsec(lba_base + sec_idx) < 0) return done;
            }
            for (uint32_t i = 0; i < chunk; i++)
                sec[sec_off + i] = src[done + written_in + i];
            if (wsec(lba_base + sec_idx) < 0) return done;

            written_in += chunk;
        }
        done += to_write;
    }

    /* Update file size and (if needed) first_cluster in the directory entry. */
    uint32_t new_end = pos + done;
    if (new_end > *file_size) *file_size = new_end;

    if (cluster_changed || new_end > pos)  /* always rewrite dirent on any write */
        update_dirent(dir_lba, dir_off, *first_cluster_ptr, *file_size);

    kprintf("fat16: write done=%u size=%u clus=%u\n", done, *file_size, *first_cluster_ptr);
    return done;
}

const char *fat16_getent(uint32_t idx, uint32_t *size_out) {
    if (!fs_ok) return 0;

    static char name_buf[13];
    uint32_t eps   = g_bps / 32u;
    uint32_t count = 0;

    for (uint32_t s = 0; s < g_root_secs; s++) {
        if (rsec(g_root_lba + s) < 0) return 0;

        for (uint32_t i = 0; i < eps; i++) {
            dirent_t *d = (dirent_t *)(sec + i * 32u);
            uint8_t first = (uint8_t)d->name[0];

            if (first == 0x00) return 0;
            if (first == 0xE5) continue;
            if (d->attr & (ATTR_VOLUME | ATTR_SUBDIR)) continue;

            if (count == idx) {
                from_83(d->name, d->ext, name_buf);
                if (size_out) *size_out = d->size;
                return name_buf;
            }
            count++;
        }
    }
    return 0;
}
