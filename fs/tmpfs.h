#ifndef TMPFS_H
#define TMPFS_H

#include <stdint.h>

#define TMPFS_MAX_FILES  16u
#define TMPFS_NAME_MAX   32u
#define TMPFS_INIT_CAP   256u

/*
 * A single named file in the in-memory writable filesystem.
 * data/size/cap are managed by tmpfs_write(); the entry persists
 * until the kernel is rebooted (there is no unlink yet).
 */
typedef struct {
    char     name[TMPFS_NAME_MAX];
    uint8_t *data;
    uint32_t size;  /* bytes of valid content */
    uint32_t cap;   /* allocated capacity     */
    uint8_t  used;
} tmpfs_entry_t;

void           tmpfs_init(void);

/* Find an existing entry by name, or NULL. */
tmpfs_entry_t *tmpfs_find(const char *name);

/*
 * Find or create an entry, truncating its content to zero bytes.
 * Returns NULL on allocation failure.
 */
tmpfs_entry_t *tmpfs_create(const char *name);

/*
 * Write len bytes from buf at byte offset pos inside the entry,
 * growing the backing buffer as needed.
 * Returns bytes written, or 0 on failure.
 */
uint32_t tmpfs_write(tmpfs_entry_t *e, const void *buf, uint32_t len,
                     uint32_t pos);

/* Return the idx-th used entry (for vfs_getent), or NULL. */
tmpfs_entry_t *tmpfs_get_by_idx(uint32_t idx);

#endif /* TMPFS_H */
