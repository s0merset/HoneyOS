#ifndef DISK_H
#define DISK_H

#include <stdint.h>

extern void read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
extern void write_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);

#endif
