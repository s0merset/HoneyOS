#include "../include/disk.h"

/*
 * disk.c — ATA PIO (Programmed I/O) Disk Driver
 *
 * This is HoneyOS's lowest-level driver: it communicates directly
 * with the virtual hard disk using x86 I/O port instructions.
 *
 * ATA PIO Mode 28-bit LBA protocol:
 *   Port 0x1F0 — Data register (read/write 16-bit words)
 *   Port 0x1F2 — Sector count
 *   Port 0x1F3 — LBA bits 0–7  (low byte)
 *   Port 0x1F4 — LBA bits 8–15 (mid byte)
 *   Port 0x1F5 — LBA bits 16–23 (high byte)
 *   Port 0x1F6 — Drive/head register (0xE0 = LBA mode, master drive)
 *   Port 0x1F7 — Command register (write) / Status register (read)
 *
 * Status register bits:
 *   Bit 7 (BSY) — Controller is busy; wait before sending commands
 *   Bit 3 (DRQ) — Data Request; drive is ready to transfer data
 */

/* ── Inline I/O Port Helpers ── */

/* outb — Write a byte to an x86 I/O port */
static void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* inb — Read a byte from an x86 I/O port */
static uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* inw — Read a 16-bit word from an x86 I/O port (used for reading sector data) */
static uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* outw — Write a 16-bit word to an x86 I/O port (used for writing sector data) */
static void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/*
 * read_sectors — Read one or more 512-byte sectors from the virtual disk.
 *
 *   lba    : Logical Block Address of the first sector to read
 *   count  : How many consecutive sectors to read
 *   buffer : Output buffer (must hold at least count * 512 bytes)
 *
 * For each sector:
 *   1. Wait for the drive to be idle (BSY bit clear)
 *   2. Send the LBA address and sector count to the ATA registers
 *   3. Send the READ SECTORS command (0x20)
 *   4. Wait for the drive to signal data is ready (DRQ bit set)
 *   5. Read 256 16-bit words (= 512 bytes) from the data port
 */
void read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t sector = 0; sector < count; sector++) {
        /* Step 1: Wait until BSY (bit 7) clears — drive is not busy */
        while (inb(0x1F7) & 0x80);

        uint32_t current_lba = lba + sector;

        /* Step 2: Program ATA registers with LBA address */
        outb(0x1F6, 0xE0 | ((current_lba >> 24) & 0x0F)); /* LBA mode + master + LBA bits 24-27 */
        outb(0x1F2, 1);                                   /* Read exactly 1 sector at a time */
        outb(0x1F3, (uint8_t)current_lba);                /* LBA bits 0-7 */
        outb(0x1F4, (uint8_t)(current_lba >> 8));         /* LBA bits 8-15 */
        outb(0x1F5, (uint8_t)(current_lba >> 16));        /* LBA bits 16-23 */
        outb(0x1F7, 0x20);                                /* Command: READ SECTORS (PIO) */

        /* Step 3: Wait for DRQ (bit 3) — drive has data ready for us to read */
        while (!(inb(0x1F7) & 0x08));

        /* Step 4: Read 256 words = 512 bytes into buffer */
        uint16_t *ptr = (uint16_t*)(buffer + (sector * 512));
        for (int i = 0; i < 256; i++) {
            ptr[i] = inw(0x1F0);    /* Each inw pulls 2 bytes from the data register */
        }
    }
}

/*
 * write_sectors — Write one or more 512-byte sectors to the virtual disk.
 *
 *   lba    : Logical Block Address of the first sector to write
 *   count  : How many consecutive sectors to write
 *   buffer : Source buffer (must hold at least count * 512 bytes)
 *
 * For each sector:
 *   1. Wait for BSY to clear
 *   2. Send LBA address and sector count
 *   3. Send the WRITE SECTORS command (0x30)
 *   4. Wait for DRQ — drive is ready to accept data
 *   5. Write 256 words to the data port
 *   6. Send cache flush command (0xE7) and wait for it to complete
 */
void write_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t sector = 0; sector < count; sector++) {
        /* Step 1: Wait until drive is idle */
        while (inb(0x1F7) & 0x80);

        uint32_t current_lba = lba + sector;

        /* Step 2: Program ATA registers */
        outb(0x1F6, 0xE0 | ((current_lba >> 24) & 0x0F));
        outb(0x1F2, 1);
        outb(0x1F3, (uint8_t)current_lba);
        outb(0x1F4, (uint8_t)(current_lba >> 8));
        outb(0x1F5, (uint8_t)(current_lba >> 16));
        outb(0x1F7, 0x30); /* Command: WRITE SECTORS (PIO) */

        /* Step 3: Wait for DRQ — drive ready to accept data */
        while (!(inb(0x1F7) & 0x08));

        /* Step 4: Write 256 words = 512 bytes from buffer */
        uint16_t *ptr = (uint16_t*)(buffer + (sector * 512));
        for (int i = 0; i < 256; i++) {
            outw(0x1F0, ptr[i]);    /* Push 2 bytes at a time to the data register */
        }

        /* Step 5: Flush write cache to ensure data is committed to disk */
        outb(0x1F7, 0xE7); /* CACHE FLUSH command */
        while (inb(0x1F7) & 0x80);    /* Wait for flush to complete */
    }
}
