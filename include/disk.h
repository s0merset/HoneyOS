#ifndef DISK_H
#define DISK_H

#include <stdint.h>

/*
 * disk.h — ATA PIO Disk Driver API
 *
 * Declares the two low-level disk I/O functions used by the FAT32
 * filesystem layer (honeyfs.c) to read and write raw 512-byte sectors
 * directly on the virtual hard disk via x86 I/O ports.
 *
 * "LBA" stands for Logical Block Address — a flat sector index where
 * 0 = first sector of the disk, 1 = second sector, etc.
 */

/*
 * read_sectors — Read one or more 512-byte sectors from disk.
 *   lba    : Starting sector number (Logical Block Address)
 *   count  : Number of consecutive sectors to read
 *   buffer : Destination buffer; must be at least count * 512 bytes
 */
extern void read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);

/*
 * write_sectors — Write one or more 512-byte sectors to disk.
 *   lba    : Starting sector number (Logical Block Address)
 *   count  : Number of consecutive sectors to write
 *   buffer : Source buffer; must be at least count * 512 bytes
 */
extern void write_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);

#endif
