#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>

/*
 * Simple "KIRD" initrd format.
 *
 * Layout (all integers little-endian):
 *   4 bytes  magic     "KIRD" = 0x4452494B
 *   4 bytes  count     number of files
 *   For each file:
 *     28 bytes  name   null-padded filename
 *      4 bytes  size   byte length of file data
 *      N bytes  data   raw file contents (no padding)
 */

#define INITRD_MAGIC  0x4452494Bu  /* "KIRD" */
#define INITRD_NAMELEN 28

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t count;
} initrd_header_t;

typedef struct __attribute__((packed)) {
    char     name[INITRD_NAMELEN];
    uint32_t size;
    /* data follows immediately */
} initrd_entry_t;

#endif
