#include "vfs.h"
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
}

vfs_file_t *vfs_open(const char *name) {
    if (!initrd_base) return NULL;

    const initrd_header_t *hdr = (const initrd_header_t *)initrd_base;
    const uint8_t *p = initrd_base + sizeof(initrd_header_t);
    const uint8_t *end = initrd_base + initrd_size;

    for (uint32_t i = 0; i < hdr->count; i++) {
        if (p + sizeof(initrd_entry_t) > end) break;
        const initrd_entry_t *e = (const initrd_entry_t *)p;
        const uint8_t *data = p + sizeof(initrd_entry_t);

        if (data + e->size > end) break;

        if (kstrcmp(e->name, name) == 0) {
            vfs_file_t *f = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
            if (!f) return NULL;
            f->pos  = 0;
            f->size = e->size;
            f->data = data;
            return f;
        }
        p = data + e->size;
    }
    return NULL;
}

uint32_t vfs_read(vfs_file_t *f, void *buf, uint32_t len) {
    if (!f || !buf) return 0;
    uint32_t avail = f->size - f->pos;
    if (len > avail) len = avail;
    uint8_t *dst = (uint8_t *)buf;
    const uint8_t *src = f->data + f->pos;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
    f->pos += len;
    return len;
}

void vfs_close(vfs_file_t *f) {
    kfree(f);
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
