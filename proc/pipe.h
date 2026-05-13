#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_BUFSZ 4096u

typedef struct pipe {
    uint8_t  buf[PIPE_BUFSZ];
    uint32_t r;       /* read cursor (next byte to consume) */
    uint32_t w;       /* write cursor (next byte to produce) */
    uint32_t len;     /* bytes available to read */
    uint32_t writers; /* number of open write-end fds */
    uint32_t readers; /* number of open read-end fds */
} pipe_t;

/* Allocate a new pipe; both writer and reader counts start at 1. */
pipe_t  *pipe_alloc(void);

/*
 * Read up to len bytes from the pipe into dst.
 * Returns bytes consumed (may be less than len); never blocks.
 * Returns 0 if the pipe is empty.
 */
int32_t  pipe_read (pipe_t *p, void *dst, uint32_t len);

/*
 * Write up to len bytes from src into the pipe.
 * Returns bytes written (may be less than len if full); never blocks.
 */
int32_t  pipe_write(pipe_t *p, const void *src, uint32_t len);

/* Decrement the appropriate refcount; free the pipe when both hit 0. */
void     pipe_close_reader(pipe_t *p);
void     pipe_close_writer(pipe_t *p);

#endif /* PIPE_H */
