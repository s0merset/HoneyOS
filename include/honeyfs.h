#ifndef HONEYFS_H
#define HONEYFS_H

#include <stdint.h>

/* FAT32 Configuration */
#define FS_MAX_FILENAME     32
#define FS_MAX_FILESIZE     4096

/* Return Codes */
#define FS_OK               0
#define FS_ERR_NOT_FOUND   -1
#define FS_ERR_EXISTS      -2
#define FS_ERR_FULL        -3
#define FS_ERR_NAME        -4
#define FS_ERR_SIZE        -5
#define FS_ERR_DISK        -6
#define FS_ERR_CORRUPT     -7

/* FAT32 Directory Entry Structure (packed) */
typedef struct __attribute__((packed)) {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attributes;
    uint8_t  reserved[10];
    uint16_t cluster_high;
    uint16_t time;
    uint16_t date;
    uint16_t cluster_low;
    uint32_t file_size;
} FAT32DirEntry;

/* The Filesystem Context (replaces the old array-based struct) */
typedef struct {
    uint32_t partition_lba;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t root_dir_cluster;
    uint32_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint8_t  num_fats;
    uint32_t sectors_per_fat;
    uint32_t total_clusters;
    uint32_t next_free_cluster;
    int      mounted;
} HoneyFS;

/* Function Prototypes */
void fs_init(HoneyFS *fs);
int  fs_create(HoneyFS *fs, const char *name);
int  fs_write(HoneyFS *fs, const char *name, const char *content);
int  fs_read(HoneyFS *fs, const char *name, char *out_buf);
int  fs_delete(HoneyFS *fs, const char *name);
void fs_list(HoneyFS *fs);

/* Internal Helpers (needed for disk traversal) */
uint32_t fs_cluster_to_lba(HoneyFS *fs, uint32_t cluster);
int      fs_find_entry(HoneyFS *fs, const char *name, FAT32DirEntry *out_entry);

#endif
