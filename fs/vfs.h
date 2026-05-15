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

/* Simple stat structure returned by vfs_stat / SYS_STAT. */
typedef struct {
    uint32_t size;
    uint32_t type;   /* 0 = file, 1 = directory */
} vfs_stat_t;

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

/* Seek within an open file.  whence: 0=SET 1=CUR 2=END.  Returns new pos. */
int32_t vfs_lseek(vfs_file_t *f, int32_t offset, int whence);

/* Free the vfs_file_t without destroying the underlying file. */
void vfs_close(vfs_file_t *f);

/* Delete a file.  Returns 0 on success. */
int vfs_unlink(const char *name);

/* Create a directory.  Returns 0 on success. */
int vfs_mkdir(const char *name);

/* Change the current working directory.  Returns 0 on success. */
int vfs_chdir(const char *path);

/* Copy the current working directory path into buf (up to size bytes). */
void vfs_getcwd(char *buf, uint32_t size);

/* Stat a file or directory by name.  Returns 0 on success. */
int vfs_stat(const char *name, vfs_stat_t *st);

/* Return the filename of the idx-th entry in the current directory.
 * (initrd entries are always included; FAT/tmpfs use the cwd.) */
const char *vfs_getent(uint32_t idx);

typedef void (*vfs_list_cb_t)(const char *name, uint32_t size, void *ud);
void vfs_list(vfs_list_cb_t cb, void *ud);

#endif /* VFS_H */
