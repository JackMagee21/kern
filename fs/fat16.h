#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>

int fat16_init(void);
int fat16_present(void);

/*
 * Path-based operations.  cwd_cluster is the calling task's current working
 * directory cluster (0 = root).  Relative paths are resolved from there;
 * absolute paths (leading '/') always start from root.
 */

/* Find a file.  Fills *first_cluster, *size, *dir_lba, *dir_off on success. */
int fat16_find(const char *path, uint16_t cwd_cluster,
               uint16_t *first_cluster, uint32_t *size,
               uint32_t *dir_lba, uint32_t *dir_off);

/* Create or truncate a file.  Fills *first_cluster (0 for new), *dir_lba, *dir_off. */
int fat16_create(const char *path, uint16_t cwd_cluster,
                 uint16_t *first_cluster, uint32_t *dir_lba, uint32_t *dir_off);

/* Delete a file (marks dirent 0xE5, frees cluster chain). */
int fat16_delete(const char *path, uint16_t cwd_cluster);

/* Create a subdirectory.  Returns 0 on success. */
int fat16_mkdir(const char *path, uint16_t cwd_cluster);

/* Resolve path as a directory; fills *new_cluster_out (0 = root). */
int fat16_chdir(const char *path, uint16_t cwd_cluster, uint16_t *new_cluster_out);

/* Cluster-based read / write (used by vfs layer after open). */
uint32_t fat16_read(uint16_t first_cluster, uint32_t pos, void *buf, uint32_t len);

uint32_t fat16_write(uint16_t *first_cluster_ptr,
                     uint32_t dir_lba, uint32_t dir_off,
                     uint32_t pos, const void *buf, uint32_t len,
                     uint32_t *file_size);

/*
 * Enumerate a directory.  Returns the name of the idx-th entry, or NULL when
 * exhausted.  Skips ".", "..", deleted entries, and volume labels.
 * size_out and is_dir_out may be NULL.
 */
const char *fat16_getent(uint32_t idx, uint16_t dir_cluster,
                          uint32_t *size_out, int *is_dir_out);

#endif
