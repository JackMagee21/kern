#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/* Magic value placed in eax by the bootloader. */
#define MULTIBOOT_MAGIC 0x2BADB002

/* Flags in multiboot_info_t.flags */
#define MULTIBOOT_FLAG_MEM  (1 << 0)   /* mem_lower / mem_upper valid */
#define MULTIBOOT_FLAG_MMAP (1 << 6)   /* mmap_length / mmap_addr valid */

/*
 * The Multiboot information structure handed to us in ebx by GRUB.
 * Only the fields we actually use are listed; the rest are covered by
 * the syms[] padding.
 *
 * Offsets (all uint32_t fields are 4 bytes):
 *   0   flags
 *   4   mem_lower
 *   8   mem_upper
 *  12   boot_device
 *  16   cmdline
 *  20   mods_count
 *  24   mods_addr
 *  28   syms[16]   (a.out or ELF symbol-table info — 16 bytes)
 *  44   mmap_length
 *  48   mmap_addr
 */
typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;    /* KB below 1 MB */
    uint32_t mem_upper;    /* KB above 1 MB */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint8_t  syms[16];
    uint32_t mmap_length;  /* byte length of the mmap buffer */
    uint32_t mmap_addr;    /* physical address of the first entry */
} multiboot_info_t;

/*
 * One entry in the memory map.
 * The size field gives the length of the rest of the entry (i.e. it does NOT
 * include itself).  Stride to the next entry = size + 4.
 */
typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;   /* 1 = available RAM */
} multiboot_mmap_entry_t;

#define MULTIBOOT_MMAP_AVAILABLE 1

#endif
