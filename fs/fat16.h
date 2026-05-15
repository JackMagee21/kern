#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>

/* Initialise from the primary ATA drive.  Returns 0 on success. */
int fat16_init(void);
int fat16_present(void);

/*
 * Find a file in the root directory by name (case-insensitive, 8.3 format).
 * On success: fills *first_cluster, *size, *dir_lba, *dir_off (byte offset
 * of the 32-byte dirent within its sector).
 * Returns 0 on success, -1 if not found.
 */
int fat16_find(const char *name,
               uint16_t *first_cluster, uint32_t *size,
               uint32_t *dir_lba, uint32_t *dir_off);

/*
 * Create or truncate a root-directory file.
 * On success: fills *first_cluster (0 for a freshly created file), *dir_lba,
 * *dir_off.  Returns 0 on success, -1 on failure.
 */
int fat16_create(const char *name,
                 uint16_t *first_cluster,
                 uint32_t *dir_lba, uint32_t *dir_off);

/*
 * Read up to len bytes from file starting at byte offset pos.
 * Returns bytes actually read.
 */
uint32_t fat16_read(uint16_t first_cluster, uint32_t pos,
                    void *buf, uint32_t len);

/*
 * Write len bytes at byte offset pos.  Allocates clusters as needed.
 * *first_cluster_ptr is updated if the file had no clusters yet.
 * *file_size is updated to max(old_size, pos+written).
 * The directory entry at (dir_lba, dir_off) is updated on disk.
 * Returns bytes written.
 */
uint32_t fat16_write(uint16_t *first_cluster_ptr,
                     uint32_t dir_lba, uint32_t dir_off,
                     uint32_t pos, const void *buf, uint32_t len,
                     uint32_t *file_size);

/*
 * Return the printable name of the idx-th non-empty, non-deleted,
 * non-directory root-directory entry.  Writes into a static buffer.
 * size_out may be NULL.  Returns NULL when idx is out of range.
 */
const char *fat16_getent(uint32_t idx, uint32_t *size_out);

#endif
