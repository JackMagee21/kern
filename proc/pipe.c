#include "pipe.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>

pipe_t *pipe_alloc(void) {
    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!p) return NULL;
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < sizeof(pipe_t); i++) b[i] = 0;
    p->writers = 1;
    p->readers = 1;
    return p;
}

int32_t pipe_read(pipe_t *p, void *dst, uint32_t len) {
    if (!p || !dst || !len) return 0;
    if (len > p->len) len = p->len;
    if (!len) return 0;

    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < len; i++) {
        d[i]  = p->buf[p->r];
        p->r  = (p->r + 1u) % PIPE_BUFSZ;
    }
    p->len -= len;
    return (int32_t)len;
}

int32_t pipe_write(pipe_t *p, const void *src, uint32_t len) {
    if (!p || !src || !len) return 0;
    uint32_t space = PIPE_BUFSZ - p->len;
    if (len > space) len = space;
    if (!len) return 0;

    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < len; i++) {
        p->buf[p->w] = s[i];
        p->w = (p->w + 1u) % PIPE_BUFSZ;
    }
    p->len += len;
    return (int32_t)len;
}

void pipe_close_reader(pipe_t *p) {
    if (!p) return;
    if (p->readers) p->readers--;
    if (!p->readers && !p->writers) kfree(p);
}

void pipe_close_writer(pipe_t *p) {
    if (!p) return;
    if (p->writers) p->writers--;
    if (!p->readers && !p->writers) kfree(p);
}
