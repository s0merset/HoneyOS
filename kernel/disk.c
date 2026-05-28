#include "../include/disk.h"

// Basic I/O functions (you might have these in another file, 
// if so, include that header instead of defining these here)
static void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t sector = 0; sector < count; sector++) {
        // 1. Wait for drive to be ready
        while (inb(0x1F7) & 0x80);

        uint32_t current_lba = lba + sector;

        // 2. Send command to Primary Master
        outb(0x1F6, 0xE0 | ((current_lba >> 24) & 0x0F)); // LBA high bits + Master bit
        outb(0x1F2, 1);                                   // Sector count
        outb(0x1F3, (uint8_t)current_lba);                // LBA low
        outb(0x1F4, (uint8_t)(current_lba >> 8));         // LBA mid
        outb(0x1F5, (uint8_t)(current_lba >> 16));        // LBA high
        outb(0x1F7, 0x20);                                // Command: READ SECTORS

        // 3. Wait for DRQ (Data Request)
        while (!(inb(0x1F7) & 0x08));

        // 4. Read 256 words (512 bytes)
        uint16_t *ptr = (uint16_t*)(buffer + (sector * 512));
        for (int i = 0; i < 256; i++) {
            ptr[i] = inw(0x1F0);
        }
    }
}

void write_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t sector = 0; sector < count; sector++) {
        // 1. Wait for drive to be ready
        while (inb(0x1F7) & 0x80);

        uint32_t current_lba = lba + sector;

        // 2. Send Command
        outb(0x1F6, 0xE0 | ((current_lba >> 24) & 0x0F));
        outb(0x1F2, 1);
        outb(0x1F3, (uint8_t)current_lba);
        outb(0x1F4, (uint8_t)(current_lba >> 8));
        outb(0x1F5, (uint8_t)(current_lba >> 16));
        outb(0x1F7, 0x30); // Command: WRITE SECTORS

        // 3. Wait for DRQ
        while (!(inb(0x1F7) & 0x08));

        // 4. Write 256 words
        uint16_t *ptr = (uint16_t*)(buffer + (sector * 512));
        for (int i = 0; i < 256; i++) {
            outw(0x1F0, ptr[i]);
        }

        outb(0x1F7, 0xE7); // Cache flush
        while (inb(0x1F7) & 0x80);
    }
}
