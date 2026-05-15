#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

/* Detect the primary master ATA drive.  Returns 0 on success, -1 if absent. */
int ata_init(void);
int ata_present(void);

/* Read/write contiguous LBA sectors.  buf must be count*512 bytes.
 * Returns 0 on success, -1 on error. */
int ata_read(uint32_t lba, uint32_t count, void *buf);
int ata_write(uint32_t lba, uint32_t count, const void *buf);

#endif
