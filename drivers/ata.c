#include "ata.h"
#include "io.h"
#include <stdint.h>

/* Primary ATA channel (I/O base 0x1F0, control 0x3F6). */
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECT_CNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE_SEL  0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALT_STATUS 0x3F6

#define SR_BSY  0x80
#define SR_DRDY 0x40
#define SR_DRQ  0x08
#define SR_ERR  0x01

#define CMD_READ_SECTS  0x20
#define CMD_WRITE_SECTS 0x30
#define CMD_IDENTIFY    0xEC

static int disk_ok = 0;

/* Four reads of the alternate status register ≈ 400 ns delay. */
static void delay400(void) {
    inb(ATA_ALT_STATUS); inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS); inb(ATA_ALT_STATUS);
}

static int wait_not_busy(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & SR_ERR)  return -1;
        if (!(s & SR_BSY)) return 0;
    }
    return -1;
}

static int wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & SR_ERR)  return -1;
        if (s & SR_DRQ)  return 0;
    }
    return -1;
}

int ata_init(void) {
    /* Select master on primary channel. */
    outb(ATA_DRIVE_SEL, 0xA0);
    delay400();

    /* If status reads 0xFF, no drive present (floating bus). */
    if (inb(ATA_STATUS) == 0xFF) { disk_ok = 0; return -1; }

    /* Issue IDENTIFY. */
    outb(ATA_SECT_CNT, 0);
    outb(ATA_LBA_LO,   0);
    outb(ATA_LBA_MID,  0);
    outb(ATA_LBA_HI,   0);
    outb(ATA_COMMAND, CMD_IDENTIFY);
    delay400();

    if (inb(ATA_STATUS) == 0) { disk_ok = 0; return -1; }

    if (wait_not_busy() < 0) { disk_ok = 0; return -1; }

    /* Non-zero mid/hi bytes mean ATAPI or other non-ATA device. */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
        disk_ok = 0; return -1;
    }

    if (wait_drq() < 0) { disk_ok = 0; return -1; }

    /* Drain the 256-word IDENTIFY buffer. */
    for (int i = 0; i < 256; i++) inw(ATA_DATA);

    disk_ok = 1;
    return 0;
}

int ata_present(void) { return disk_ok; }

int ata_read(uint32_t lba, uint32_t count, void *buf) {
    if (!disk_ok || !count) return -1;
    uint16_t *p = (uint16_t *)buf;

    while (count--) {
        if (wait_not_busy() < 0) return -1;

        outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F)); /* LBA28, master */
        outb(ATA_SECT_CNT,  1);
        outb(ATA_LBA_LO,   (uint8_t)(lba));
        outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
        outb(ATA_LBA_HI,   (uint8_t)(lba >> 16));
        outb(ATA_COMMAND, CMD_READ_SECTS);

        delay400();
        if (wait_drq() < 0) return -1;

        for (int j = 0; j < 256; j++) *p++ = inw(ATA_DATA);
        lba++;
    }
    return 0;
}

int ata_write(uint32_t lba, uint32_t count, const void *buf) {
    if (!disk_ok || !count) return -1;
    const uint16_t *p = (const uint16_t *)buf;

    while (count--) {
        if (wait_not_busy() < 0) return -1;

        outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_SECT_CNT,  1);
        outb(ATA_LBA_LO,   (uint8_t)(lba));
        outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
        outb(ATA_LBA_HI,   (uint8_t)(lba >> 16));
        outb(ATA_COMMAND, CMD_WRITE_SECTS);

        delay400();
        if (wait_drq() < 0) return -1;

        for (int j = 0; j < 256; j++) outw(ATA_DATA, *p++);

        /* Wait for the drive to commit the sector before the next command. */
        delay400();
        if (wait_not_busy() < 0) return -1;

        lba++;
    }
    return 0;
}
