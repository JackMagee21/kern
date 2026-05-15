#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "tmpfs.h"

typedef enum {
    VFS_INITRD = 0,
    VFS_TMPFS  = 1,
    VFS_FAT    = 2,
} vfs_src_t;

typedef struct vfs_file {
    vfs_src_t  src;
    uint32_t   pos;    /* read/write cursor */
    uint32_t   size;   /* byte length */
    union {
        const uint8_t *initrd_data;     /* pointer into in-memory initrd blob */
        tmpfs_entry_t *tmpfs_ent;
        struct {
            uint16_t first_cluster;     /* starting FAT16 cluster              */
            uint32_t dir_lba;           /* sector containing the dirent        */
            uint32_t dir_off;           /* byte offset of dirent within sector */
        } fat;
    };
} vfs_file_t;

/* Initialise the VFS from a GRUB-loaded initrd blob. */
void vfs_init(const void *initrd, uint32_t size);

/* Open a file for reading.  Search order: initrd → FAT16 → tmpfs. */
vfs_file_t *vfs_open(const char *name);

/* Create or truncate a file for writing.  Prefers FAT16 if present. */
vfs_file_t *vfs_create(const char *name);

/* Open a file for appending.  Prefers FAT16 if present. */
vfs_file_t *vfs_open_append(const char *name);

/* Read up to len bytes; advances pos.  Returns bytes read. */
uint32_t vfs_read(vfs_file_t *f, void *buf, uint32_t len);

/* Write len bytes at the current position.  Returns bytes written. */
uint32_t vfs_write(vfs_file_t *f, const void *buf, uint32_t len);

/* Free the vfs_file_t without destroying the underlying file. */
void vfs_close(vfs_file_t *f);

typedef void (*vfs_list_cb_t)(const char *name, uint32_t size, void *ud);
void vfs_list(vfs_list_cb_t cb, void *ud);

/* Return the filename of the idx-th entry (initrd, then FAT16, then tmpfs). */
const char *vfs_getent(uint32_t idx);

#endif /* VFS_H */
