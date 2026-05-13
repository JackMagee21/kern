#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

/* A lightweight in-memory file descriptor backed by the initrd. */
typedef struct vfs_file {
    uint32_t        pos;    /* current read position */
    uint32_t        size;   /* file size in bytes     */
    const uint8_t  *data;   /* pointer into initrd blob */
} vfs_file_t;

/*
 * Initialise the VFS from an initrd blob already in kernel memory.
 * initrd: virtual pointer to the initrd image (loaded by GRUB as a module).
 * size  : byte length of the image.
 */
void vfs_init(const void *initrd, uint32_t size);

/* Open a file by name; returns NULL if not found. */
vfs_file_t *vfs_open(const char *name);

/* Read up to len bytes from f into buf; advances f->pos.  Returns bytes read. */
uint32_t vfs_read(vfs_file_t *f, void *buf, uint32_t len);

/* Close (free) a file descriptor returned by vfs_open. */
void vfs_close(vfs_file_t *f);

/*
 * Iterate over every file in the initrd.
 * Calls cb(name, size, userdata) for each entry.
 * Safe to call before vfs_init (does nothing).
 */
typedef void (*vfs_list_cb_t)(const char *name, uint32_t size, void *ud);
void vfs_list(vfs_list_cb_t cb, void *ud);

/*
 * Return the filename of the idx-th entry (0-based), or NULL if out of range.
 * Used by SYS_GETDENT so userland can iterate the initrd without a callback.
 */
const char *vfs_getent(uint32_t idx);

#endif
