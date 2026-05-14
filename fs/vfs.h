#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "tmpfs.h"

typedef enum { VFS_INITRD = 0, VFS_TMPFS = 1 } vfs_src_t;

/*
 * Unified file descriptor for both read-only initrd files and writable
 * tmpfs files.  vfs_read / vfs_write dispatch on src.
 */
typedef struct vfs_file {
    vfs_src_t  src;
    uint32_t   pos;   /* read/write position */
    uint32_t   size;  /* byte length (for initrd; tmpfs uses entry->size) */
    union {
        const uint8_t *initrd_data; /* pointer into the in-memory initrd blob */
        tmpfs_entry_t *tmpfs_ent;   /* pointer into the tmpfs table           */
    };
} vfs_file_t;

/* Initialise the VFS from a GRUB-loaded initrd blob. */
void vfs_init(const void *initrd, uint32_t size);

/* Open a file for reading.  Searches initrd first, then tmpfs. */
vfs_file_t *vfs_open(const char *name);

/* Create or truncate a tmpfs file and open it for writing. */
vfs_file_t *vfs_create(const char *name);

/* Open an existing tmpfs file for appending; returns NULL if not found. */
vfs_file_t *vfs_open_append(const char *name);

/* Read up to len bytes; advances pos.  Returns bytes read. */
uint32_t vfs_read(vfs_file_t *f, void *buf, uint32_t len);

/* Write len bytes at the current position (tmpfs files only). */
uint32_t vfs_write(vfs_file_t *f, const void *buf, uint32_t len);

/* Free the vfs_file_t.  Does NOT destroy the underlying tmpfs entry. */
void vfs_close(vfs_file_t *f);

typedef void (*vfs_list_cb_t)(const char *name, uint32_t size, void *ud);
void vfs_list(vfs_list_cb_t cb, void *ud);

/* Return the filename of the idx-th entry (initrd first, then tmpfs). */
const char *vfs_getent(uint32_t idx);

#endif /* VFS_H */
