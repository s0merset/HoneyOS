#ifndef FAT32_H
#define FAT32_H

/* ============================================================
 * FAT32 Filesystem Driver - fat32.h
 * HoneyOS | CMSC 125 - Phase 2
 *
 * FAT32 Disk Layout (virtual hard disk):
 *
 *  Sector 0          : Boot Sector (BPB - BIOS Parameter Block)
 *  Sector 1          : FSInfo Sector (free cluster count cache)
 *  Sector 2-5        : Reserved sectors
 *  Sector 6          : Backup Boot Sector (copy of sector 0)
 *  Sectors R+0..R+F  : FAT Table 1 (File Allocation Table)
 *  Sectors R+F..R+2F : FAT Table 2 (backup copy)
 *  Cluster 2+        : Data Region (root dir is cluster 2)
 *
 * Key differences from FAT12/FAT16:
 *  - FAT entries are 32 bits (28 bits used, top 4 reserved)
 *  - Root directory is a normal cluster chain (not fixed region)
 *  - Supports volumes up to 2TB
 *  - FSInfo sector caches free cluster count for speed
 *  - Cluster numbers are 32-bit
 * ============================================================ */

/* ── Disk geometry (128MB virtual disk) ── */
#define FAT32_BYTES_PER_SECTOR       512
#define FAT32_SECTORS_PER_CLUSTER    8       /* 8 * 512 = 4096 byte clusters */
#define FAT32_RESERVED_SECTORS       32      /* Sectors before FAT1           */
#define FAT32_NUM_FATS               2       /* Always 2 FAT copies           */
#define FAT32_TOTAL_SECTORS          262144  /* 128MB / 512 = 256K sectors    */
#define FAT32_SECTORS_PER_FAT        256     /* FAT table size in sectors     */
#define FAT32_ROOT_CLUSTER           2       /* Root dir starts at cluster 2  */
#define FAT32_SECTORS_PER_TRACK      63
#define FAT32_NUM_HEADS              255
#define FAT32_MEDIA_DESCRIPTOR       0xF8   /* Fixed disk                    */
#define FAT32_FSINFO_SECTOR          1       /* FSInfo at sector 1            */
#define FAT32_BACKUP_BOOT_SECTOR     6       /* Backup boot at sector 6       */

/* ── Derived layout values ── */
#define FAT32_FAT1_START     FAT32_RESERVED_SECTORS    /* FAT1 begins right after the reserved sectors */
#define FAT32_FAT2_START     (FAT32_FAT1_START + FAT32_SECTORS_PER_FAT)    /* FAT2 is a mirror of FAT1, placed directly after it */
#define FAT32_DATA_START     (FAT32_FAT2_START + FAT32_SECTORS_PER_FAT)    /* Data clusters start immediately after both FAT copies */

/* ── FAT32 special cluster values ── */
#define FAT32_CLUSTER_FREE    0x00000000    /* Cluster is unallocated and available */
#define FAT32_CLUSTER_RSVD    0x00000001    /* Reserved — never used for file data */
#define FAT32_CLUSTER_BAD     0x0FFFFFF7    /* Cluster is physically damaged; skip it */
#define FAT32_CLUSTER_EOF     0x0FFFFFFF    /* End of chain — this is the last cluster of a file */
#define FAT32_CLUSTER_MASK    0x0FFFFFFF    /* Mask off the top 4 reserved bits when reading FAT entries */
#define FAT32_MIN_DATA        2             /* Cluster numbers 0 and 1 are reserved; user data starts at 2 */
#define FAT32_MAX_CLUSTER     (FAT32_TOTAL_SECTORS / FAT32_SECTORS_PER_CLUSTER)    /* Total number of clusters on the disk */

/* ── Directory entry attributes ── */
#define FAT32_ATTR_READ_ONLY  0x01    /* File cannot be written to */
#define FAT32_ATTR_HIDDEN     0x02    /* File is hidden from normal directory listings */
#define FAT32_ATTR_SYSTEM     0x04    /* OS system file — should not be moved */
#define FAT32_ATTR_VOLUME_ID  0x08    /* This entry is the volume label, not a file */
#define FAT32_ATTR_DIRECTORY  0x10    /* This entry points to a subdirectory cluster chain */
#define FAT32_ATTR_ARCHIVE    0x20    /* Normal file — set when a file is created or modified */
#define FAT32_ATTR_LFN        0x0F    /* Long filename entry — all four lower attribute bits set */

/* ── Directory entry first-byte status ── */
#define FAT32_ENTRY_FREE      0x00  /* This slot has never been used; marks end of directory scan */
#define FAT32_ENTRY_DELETED   0xE5  /* File was deleted; slot can be reused */

/* ── FSInfo signatures ── */
#define FAT32_FSINFO_SIG1     0x41615252    /* "RRaA" — lead signature at offset 0 */
#define FAT32_FSINFO_SIG2     0x61417272    /* "rrAa" — structure signature at offset 484 */
#define FAT32_FSINFO_SIG3     0xAA550000    /* Trail signature at offset 508 — validates the sector */

/* ── Return codes ── */
#define FAT32_OK              0    /* Operation completed successfully */
#define FAT32_ERR_NOT_FOUND  -1    /* File does not exist in the root directory */
#define FAT32_ERR_EXISTS     -2    /* A file with the same name already exists */
#define FAT32_ERR_FULL       -3    /* No free directory slots or disk clusters available */
#define FAT32_ERR_NAME       -4    /* Filename is invalid (too long, bad characters, etc.) */
#define FAT32_ERR_SIZE       -5    /* File content exceeds FAT32_MAX_FILESIZE */
#define FAT32_ERR_DISK       -6    /* ATA disk I/O operation failed */
#define FAT32_ERR_CORRUPT    -7    /* FAT32 structure is inconsistent or unreadable */
#define FAT32_ERR_NOINIT     -8    /* fat32_init() has not been called yet */

/* ── Limits ── */
#define FAT32_MAX_FILESIZE    (FAT32_SECTORS_PER_CLUSTER * FAT32_BYTES_PER_SECTOR * 8)    /* Max bytes per file = 8 clusters = 32KB */
#define FAT32_MAX_FILENAME    8     /* 8.3 format: up to 8 characters in the name part */
#define FAT32_MAX_EXT         3     /* 8.3 format: up to 3 characters in the extension */

/* ── Root dir entries cached in RAM ── */
#define FAT32_ROOT_CACHE_ENTRIES  128    /* Max files in root directory held in RAM at once */


/* ============================================================
 * BIOS PARAMETER BLOCK (Boot Sector - Sector 0)
 * FAT32 Extended BPB structure
 * ============================================================ */
typedef struct {
    /* DOS 2.0 BPB */
    unsigned char  jump[3];               /* JMP SHORT xx NOP              */
    unsigned char  oem_name[8];           /* "HoneyOS "                    */
    unsigned short bytes_per_sector;      /* 512                           */
    unsigned char  sectors_per_cluster;   /* Power of 2                    */
    unsigned short reserved_sectors;      /* Before FAT1 (32)              */
    unsigned char  num_fats;              /* 2                             */
    unsigned short root_entry_count;      /* 0 for FAT32                   */
    unsigned short total_sectors_16;      /* 0 for FAT32 (use 32-bit)      */
    unsigned char  media_descriptor;      /* 0xF8 = fixed disk             */
    unsigned short sectors_per_fat_16;    /* 0 for FAT32 (use 32-bit)      */
    /* DOS 3.31 BPB */
    unsigned short sectors_per_track;     /* 63                            */
    unsigned short num_heads;             /* 255                           */
    unsigned int   hidden_sectors;        /* Sectors before partition      */
    unsigned int   total_sectors_32;      /* Total sectors (32-bit)        */
    /* FAT32 Extended BPB */
    unsigned int   sectors_per_fat_32;    /* FAT size in sectors           */
    unsigned short ext_flags;             /* Mirror flags                  */
    unsigned short fs_version;            /* 0x0000 = FAT32 version 0.0    */
    unsigned int   root_cluster;          /* First cluster of root dir (2) */
    unsigned short fs_info_sector;        /* FSInfo sector (1)             */
    unsigned short backup_boot_sector;    /* Backup boot sector (6)        */
    unsigned char  reserved[12];          /* Reserved, must be 0           */
    /* Extended Boot Record */
    unsigned char  drive_number;          /* 0x80 = first hard disk        */
    unsigned char  reserved1;             /* Reserved                      */
    unsigned char  boot_signature;        /* 0x29                          */
    unsigned int   volume_id;             /* Volume serial number          */
    unsigned char  volume_label[11];      /* "HONEYOS    "                 */
    unsigned char  fs_type[8];            /* "FAT32   "                    */
} __attribute__((packed)) FAT32BPB;

/* ============================================================
 * FSINFO SECTOR (Sector 1)
 * Caches free cluster count to speed up allocation.
 * ============================================================ */
typedef struct {
    unsigned int  signature1;          /* 0x41615252 "RRaA"               */
    unsigned char reserved1[480];      /* Reserved                        */
    unsigned int  signature2;          /* 0x61417272 "rrAa"               */
    unsigned int  free_count;          /* Free cluster count (0xFFFFFFFF = unknown) */
    unsigned int  next_free;           /* Hint for next free cluster       */
    unsigned char reserved2[12];       /* Reserved                        */
    unsigned int  trail_signature;     /* 0xAA550000                      */
} __attribute__((packed)) FAT32FSInfo;

/* ============================================================
 * SHORT DIRECTORY ENTRY (32 bytes)
 * One entry per file/directory in a cluster.
 * ============================================================ */
typedef struct {
    unsigned char  name[8];            /* Filename, space-padded          */
    unsigned char  ext[3];             /* Extension, space-padded         */
    unsigned char  attributes;         /* File attributes                 */
    unsigned char  nt_reserved;        /* Windows NT reserved (0)         */
    unsigned char  create_time_tenth;  /* Creation time tenths (0-199)    */
    unsigned short create_time;        /* hhhhhmmm mmmsssss (2s granul.)  */
    unsigned short create_date;        /* yyyyyyymmmmddddd                */
    unsigned short last_access_date;   /* Last access date                */
    unsigned short first_cluster_hi;   /* High 16 bits of first cluster   */
    unsigned short write_time;         /* Last write time                 */
    unsigned short write_date;         /* Last write date                 */
    unsigned short first_cluster_lo;   /* Low 16 bits of first cluster    */
    unsigned int   file_size;          /* File size in bytes              */
} __attribute__((packed)) FAT32DirEntry;

/* ── Helper: combine high/low cluster fields ── */
#define FAT32_FIRST_CLUSTER(e) \
    (((unsigned int)(e)->first_cluster_hi << 16) | (e)->first_cluster_lo)

#define FAT32_SET_CLUSTER(e, c) do { \
    (e)->first_cluster_hi = (unsigned short)(((c) >> 16) & 0xFFFF); \
    (e)->first_cluster_lo = (unsigned short)((c) & 0xFFFF); \
} while(0)

/* ============================================================
 * IN-MEMORY FAT32 STATE
 * Holds loaded FAT table (partial) and root dir cache in RAM.
 * ============================================================ */
typedef struct {
    /* FAT table loaded into RAM (entire FAT1) */
    unsigned int  fat_table[FAT32_SECTORS_PER_FAT *
                             FAT32_BYTES_PER_SECTOR / 4];

    /* Root directory cluster cached in RAM */
    FAT32DirEntry root_dir[FAT32_ROOT_CACHE_ENTRIES];

    /* FSInfo cache */
    unsigned int  free_count;
    unsigned int  next_free_hint;

    int           initialized;
} FAT32State;

/* ============================================================
 * FUNCTION DECLARATIONS
 * ============================================================ */

/* Initialize FAT32: format disk, write BPB/FSInfo/FAT/root dir */
int fat32_init();

/* Create a new empty file in root directory */
int fat32_create(const char *name);

/* Write data to an existing file */
int fat32_write(const char *name, const char *data, unsigned int size);

/* Read file contents into buf (must be >= FAT32_MAX_FILESIZE) */
int fat32_read(const char *name, char *buf);

/* Delete a file: free clusters + mark dir entry deleted */
int fat32_delete(const char *name);

/* List files: fills out_entries, returns count */
int fat32_list(FAT32DirEntry *out_entries, int max_entries);

/* ── Internal helpers ── */
void         fat32_format_name(const char *in, unsigned char *name, unsigned char *ext);
void         fat32_unformat_name(const unsigned char *name, const unsigned char *ext, char *out);
unsigned int fat32_get_entry(unsigned int cluster);
void         fat32_set_entry(unsigned int cluster, unsigned int value);
int          fat32_find_free_cluster();
int          fat32_find_entry(const char *name);
int          fat32_flush_fat();
int          fat32_flush_root();

#endif /* FAT32_H */
