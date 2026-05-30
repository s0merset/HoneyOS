#ifndef HONEYFS_H
#define HONEYFS_H

#include <stdint.h>

/*
 * honeyfs.h — HoneyOS FAT32 Filesystem API
 *
 * This header defines the filesystem context struct (HoneyFS),
 * the on-disk directory entry layout (FAT32DirEntry), all return
 * codes, and the public API used by main.c to operate on files.
 *
 * HoneyOS uses a real FAT32 virtual disk (honeyfs.vdi) attached to
 * VirtualBox. Files persist across reboots because they are written
 * to actual disk sectors, not held in RAM.
 */

/* ── Filesystem Limits ── */
#define FS_MAX_FILENAME     32      /* Max length of a filename string passed by the user */
#define FS_MAX_FILESIZE     4096    /* Max file content size in bytes (one FAT32 cluster) */

/* ── Return Codes ── */
#define FS_OK               0    /* Operation succeeded */
#define FS_ERR_NOT_FOUND   -1    /* File does not exist in the root directory */
#define FS_ERR_EXISTS      -2    /* A file with that name already exists */
#define FS_ERR_FULL        -3    /* No free directory slots or disk clusters available */
#define FS_ERR_NAME        -4    /* Filename is invalid (too long, spaces, etc.) */
#define FS_ERR_SIZE        -5    /* Content exceeds FS_MAX_FILESIZE */
#define FS_ERR_DISK        -6    /* Disk I/O error (ATA driver failure) */
#define FS_ERR_CORRUPT     -7    /* FAT32 structure appears invalid or inconsistent */

/*
 * FAT32DirEntry — On-disk 32-byte directory entry (packed, no padding)
 *
 * Each file in the root directory is described by exactly 32 bytes
 * laid out in this structure. This matches the FAT32 specification.
 * HoneyOS uses 8.3 short names only (e.g., "NOTES   TXT").
 */
typedef struct __attribute__((packed)) {
    uint8_t  name[8];            /* Filename, uppercase, space-padded (e.g., "NOTES   ") */
    uint8_t  ext[3];             /* Extension, uppercase, space-padded (e.g., "TXT") */
    uint8_t  attributes;         /* File attributes: 0x20 = archive (normal file) */
    uint8_t  reserved[10];       /* Reserved fields (timestamps, NT flags) — zeroed */
    uint16_t cluster_high;       /* High 16 bits of the first data cluster number */
    uint16_t time;               /* Last write time (unused, kept for spec compliance) */
    uint16_t date;               /* Last write date (unused) */
    uint16_t cluster_low;        /* Low 16 bits of the first data cluster number */
    uint32_t file_size;          /* File size in bytes (0 for empty files) */
} FAT32DirEntry;

/*
 * HoneyFS — In-memory FAT32 filesystem context
 *
 * This struct holds all the metadata read from the FAT32 Boot Sector
 * (BPB) during fs_init(). It is passed to every filesystem function
 * so they know where to find the FAT table, data clusters, etc.
 */
typedef struct {
    uint32_t partition_lba;            /* Disk sector where the FAT32 partition begins (LBA 2048) */
    uint32_t fat_start_lba;            /* First sector of the FAT table on disk */
    uint32_t data_start_lba;           /* First sector of the data region (cluster 2 onward) */
    uint32_t root_dir_cluster;         /* Cluster number of the root directory (usually 2) */
    uint32_t bytes_per_sector;         /* Should always be 512 for HoneyOS */
    uint8_t  sectors_per_cluster;      /* Number of 512-byte sectors per cluster (e.g., 8 = 4KB) */
    uint8_t  num_fats;                 /* Number of FAT copies on disk (always 2 for redundancy) */
    uint32_t sectors_per_fat;          /* Size of one FAT table in sectors */
    uint32_t total_clusters;           /* Total usable data clusters on disk */
    uint32_t next_free_cluster;        /* Hint for faster free-cluster search (starts at 3) */
    int      mounted;                  /* 1 if fs_init() succeeded and disk is valid, else 0 */
} HoneyFS;

/* ── Public API ── */

/* fs_init   — Mount the FAT32 volume by reading and validating the BPB from disk */
void fs_init(HoneyFS *fs);

/* fs_create — Create a new empty file in the root directory */
int  fs_create(HoneyFS *fs, const char *name);

/* fs_write  — Write (overwrite) content to a file; creates it if it doesn't exist */
int  fs_write(HoneyFS *fs, const char *name, const char *content);

/* fs_read   — Read a file's content into out_buf (must be >= FS_MAX_FILESIZE bytes) */
int  fs_read(HoneyFS *fs, const char *name, char *out_buf);

/* fs_delete — Delete a file: frees its FAT cluster chain and marks the dir entry deleted */
int  fs_delete(HoneyFS *fs, const char *name);

/* fs_list   — Print all files in the root directory to the VGA screen */
void fs_list(HoneyFS *fs);


/* ── Internal Helpers (used by editor.c for direct disk traversal) ── */

/* fs_cluster_to_lba — Convert a FAT32 cluster number to its disk sector (LBA) */
uint32_t fs_cluster_to_lba(HoneyFS *fs, uint32_t cluster);

/* fs_find_entry — Search the root directory for a file by name; fills out_entry if found */
int      fs_find_entry(HoneyFS *fs, const char *name, FAT32DirEntry *out_entry);

#endif
