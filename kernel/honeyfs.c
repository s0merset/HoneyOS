#include "../include/honeyfs.h"
#include "../include/disk.h"
#include "../include/honeyui.h"

/*
 * honeyfs.c — HoneyOS FAT32 Filesystem Implementation
 * CMSC 125 — Operating Systems, UP Cebu
 *
 * This file implements every filesystem operation that HoneyOS supports:
 * mounting, creating, writing, reading, deleting, and listing files.
 *
 * The filesystem lives on a separate virtual hard disk (honeyfs.vdi)
 * attached to VirtualBox. It is a real FAT32 volume — not simulated.
 * All reads and writes go through disk.c's ATA PIO driver.
 *
 * FAT32 Key Concepts Used Here:
 *   - The FAT (File Allocation Table) maps cluster numbers to the next
 *     cluster in a file's chain, or to EOF/free markers.
 *   - The root directory is a normal cluster chain starting at cluster 2.
 *   - Each directory entry is 32 bytes and holds the filename, size,
 *     first cluster number, and attribute flags.
 *   - Clusters are the smallest unit of file storage (8 sectors = 4KB here).
 */

/* ── Partition and FAT32 Constants ── */
#define FAT32_PARTITION_LBA     2048            /* The FAT32 partition starts at sector 2048 (1MiB offset) */
#define FAT32_BYTES_PER_SECTOR  512             /* Standard sector size */
#define FAT32_EOC              0x0FFFFFF8       /* End-of-chain: any value >= this means "last cluster" */
#define FAT32_EOC_MARK         0x0FFFFFFF       /* Value written to mark a cluster as the last in a chain */
#define FAT32_FREE             0x00000000       /* FAT entry value for an unallocated cluster */
#define FAT32_BAD              0x0FFFFFF7       /* FAT entry value for a bad (unusable) cluster */
#define FAT32_ATTR_ARCHIVE     0x20             /* Directory entry attribute: normal file */
#define FAT32_ATTR_DIRECTORY   0x10             /* Directory entry attribute: subdirectory */
#define FAT32_ATTR_VOLUME_ID   0x08             /* Directory entry attribute: volume label */
#define FAT32_ATTR_LFN         0x0F             /* Directory entry attribute: long filename entry (skip) */
#define FAT32_ENTRY_DELETED    0xE5             /* First byte of name = 0xE5 means this slot was deleted */

/* These are provided by main.c — honeyfs.c calls them for error output */
extern void print(const char *s, unsigned char color);
extern void vga_putchar(char c, unsigned char color);
extern void println(const char *s, unsigned char color);

/* ── Internal Memory Helpers (no stdlib available) ── */

/* fs_memset — Fill count bytes at dst with value (replaces memset) */
static void fs_memset(uint8_t *dst, uint8_t value, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) dst[i] = value;
}

/* fs_memcpy — Copy count bytes from src to dst (replaces memcpy) */
static void fs_memcpy(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) dst[i] = src[i];
}

/* ── Little-Endian Byte Readers ── */

/* rd16 — Read a 16-bit little-endian value from a raw byte pointer */
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* rd32 — Read a 32-bit little-endian value from a raw byte pointer */
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* upper — Convert a lowercase ASCII letter to uppercase */
static char upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/* content_len — Return string length up to FS_MAX_FILESIZE (no stdlib strlen) */
static int content_len(const char *content) {
    int len = 0;
    while (content[len] != '\0' && len <= FS_MAX_FILESIZE) len++;
    return len;
}

/* is_eoc — Return 1 if a FAT32 cluster value represents End-of-Chain */
static int is_eoc(uint32_t cluster) {
    return cluster >= FAT32_EOC;
}

/* first_cluster — Combine a dir entry's high+low cluster fields into one 32-bit cluster number */
static uint32_t first_cluster(FAT32DirEntry *entry) {
    return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

/* set_first_cluster — Write a 32-bit cluster number back into a dir entry's high+low fields */
static void set_first_cluster(FAT32DirEntry *entry, uint32_t cluster) {
    entry->cluster_high = (uint16_t)((cluster >> 16) & 0xFFFF);
    entry->cluster_low = (uint16_t)(cluster & 0xFFFF);
}

/*
 * to_fat_name — Convert a user-supplied filename to FAT32 8.3 short-name format.
 *
 * Input:  "notes.txt"
 * Output: fat_name[11] = "NOTES   TXT"  (8 bytes name + 3 bytes ext, space-padded)
 *
 * Returns FS_OK on success, FS_ERR_NAME if the name is invalid
 * (e.g., name part > 8 chars, extension > 3 chars, spaces, empty).
 */
static int to_fat_name(const char *name, uint8_t *fat_name) {
    for (int i = 0; i < 11; i++) fat_name[i] = ' ';    /* Pre-fill with spaces */

    int i = 0;
    int k = 0;
    /* Copy up to 8 characters before the '.' into positions 0-7 */
    while (name[i] != '.' && name[i] != '\0') {
        if (k >= 8 || name[i] == ' ') return FS_ERR_NAME;    
        fat_name[k++] = (uint8_t)upper(name[i++]);
    }

    if (k == 0) return FS_ERR_NAME;    /* Name cannot be empty */

    /* Copy up to 3 extension characters into positions 8-10 */
    if (name[i] == '.') {
        i++;
        k = 8;
        while (name[i] != '\0') {
            if (k >= 11 || name[i] == ' ') return FS_ERR_NAME;
            fat_name[k++] = (uint8_t)upper(name[i++]);
        }
    }

    return FS_OK;
}

/* ── Cluster/LBA Address Calculations ── */

/*
 * fs_cluster_to_lba — Convert a FAT32 cluster number to its starting disk sector.
 *
 * FAT32 data clusters start at cluster 2. The formula adjusts for the
 * reserved sectors and FAT tables that come before the data region.
 */
uint32_t fs_cluster_to_lba(HoneyFS *fs, uint32_t cluster) {
    return fs->data_start_lba + ((cluster - 2) * fs->sectors_per_cluster);
}

/* fat_sector_lba — Return the disk sector number that contains the FAT entry for a given cluster */
static uint32_t fat_sector_lba(HoneyFS *fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;    /* Each FAT32 entry is 4 bytes wide */
    return fs->fat_start_lba + (fat_offset / FAT32_BYTES_PER_SECTOR);
}

/* fat_entry_offset — Return the byte offset within its sector of the FAT entry for a cluster */
static int fat_entry_offset(uint32_t cluster) {
    return (int)((cluster * 4) % FAT32_BYTES_PER_SECTOR);
}

/* ── FAT Table Read/Write ── */

/*
 * fat_get — Read the FAT32 entry for a cluster number from disk.
 * Returns the next cluster in the chain, FAT32_FREE, or FAT32_EOC.
 * The top 4 bits of the 32-bit FAT entry are reserved and masked off.
 */
static uint32_t fat_get(HoneyFS *fs, uint32_t cluster) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    read_sectors(fat_sector_lba(fs, cluster), 1, sector);
    return rd32(&sector[fat_entry_offset(cluster)]) & 0x0FFFFFFF;
}

/* wr32 — Write a 32-bit little-endian value into a raw byte buffer */
static void wr32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
}

/*
 * fat_set — Write a value into the FAT entry for a cluster.
 *
 * FAT32 keeps two identical copies of the FAT table (FAT1 and FAT2)
 * for redundancy. This function updates both copies atomically.
 * The top 4 reserved bits of the existing entry are preserved.
 */
static void fat_set(HoneyFS *fs, uint32_t cluster, uint32_t value) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];

    for (uint8_t fat = 0; fat < fs->num_fats; fat++) {
        /* Calculate the LBA for this FAT copy's entry */
        uint32_t lba = fs->fat_start_lba +
            (fat * fs->sectors_per_fat) +
            ((cluster * 4) / FAT32_BYTES_PER_SECTOR);
        int offset = fat_entry_offset(cluster);

        read_sectors(lba, 1, sector);
        uint32_t old = rd32(&sector[offset]);
        /* Preserve the top 4 reserved bits; update only the 28-bit cluster value */
        wr32(&sector[offset], (old & 0xF0000000) | (value & 0x0FFFFFFF));
        write_sectors(lba, 1, sector);
    }
}

/*
 * find_free_cluster — Search the FAT for an unallocated cluster.
 *
 * Starts from next_free_cluster (a hint to avoid scanning from the beginning
 * every time). Wraps around to cluster 3 if it reaches the end.
 * Returns the cluster number on success, or FS_ERR_FULL if the disk is full.
 */
static int find_free_cluster(HoneyFS *fs) {
    uint32_t start = fs->next_free_cluster;
    if (start < 3 || start >= fs->total_clusters + 2) start = 3;

    /* First pass: from hint to end of disk */
    for (uint32_t cluster = start; cluster < fs->total_clusters + 2; cluster++) {
        if (fat_get(fs, cluster) == FAT32_FREE) {
            fs->next_free_cluster = cluster + 1;
            return (int)cluster;
        }
    }

    /* Second pass: wrap around from cluster 3 to hint */
    for (uint32_t cluster = 3; cluster < start; cluster++) {
        if (fat_get(fs, cluster) == FAT32_FREE) {
            fs->next_free_cluster = cluster + 1;
            return (int)cluster;
        }
    }

    return FS_ERR_FULL;    /* No free clusters found anywhere on disk */
}

/*
 * clear_cluster — Zero out all sectors of a cluster.
 * Called after allocating a new cluster to ensure no stale data remains.
 */
static void clear_cluster(HoneyFS *fs, uint32_t cluster) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    fs_memset(sector, 0, FAT32_BYTES_PER_SECTOR);

    uint32_t lba = fs_cluster_to_lba(fs, cluster);
    for (uint8_t i = 0; i < fs->sectors_per_cluster; i++) {
        write_sectors(lba + i, 1, sector);
    }
}

/*
 * free_chain — Walk a FAT32 cluster chain and mark every cluster as free.
 * Used by fs_delete() and fs_write() (before reallocating) to reclaim disk space.
 */
static void free_chain(HoneyFS *fs, uint32_t cluster) {
    while (cluster >= 2 && cluster < FAT32_BAD && !is_eoc(cluster)) {
        uint32_t next = fat_get(fs, cluster);    /* Read next before we overwrite this entry */
        fat_set(fs, cluster, FAT32_FREE);        /* Mark this cluster as available */
        cluster = next;
    }

    /* Handle the final EOC cluster in the chain */
    if (cluster >= 2 && cluster < FAT32_BAD) {
        fat_set(fs, cluster, FAT32_FREE);
    }
}

/* ── Root Directory I/O ── */

/* read_root — Read the first sector of the root directory cluster into a buffer */
static int read_root(HoneyFS *fs, uint8_t *sector) {
    if (!fs->mounted) return FS_ERR_DISK;
    read_sectors(fs_cluster_to_lba(fs, fs->root_dir_cluster), 1, sector);
    return FS_OK;
}

/* write_root — Write a modified root directory sector back to disk */
static int write_root(HoneyFS *fs, uint8_t *sector) {
    if (!fs->mounted) return FS_ERR_DISK;
    write_sectors(fs_cluster_to_lba(fs, fs->root_dir_cluster), 1, sector);
    return FS_OK;
}

/* ── Public API ── */

/*
 * fs_init — Mount the FAT32 volume by reading and parsing the Boot Sector (BPB).
 *
 * The BPB (BIOS Parameter Block) at the start of the partition describes
 * the entire disk layout. We read it here to populate the HoneyFS context
 * struct, which is then passed to all other fs_ functions.
 *
 * If the boot sector signature (0x55AA at bytes 510-511) is missing or
 * any critical field is zero/invalid, fs->mounted stays 0 and all
 * subsequent fs_ calls will fail gracefully with FS_ERR_DISK.
 */
void fs_init(HoneyFS *fs) {
    uint8_t buffer[FAT32_BYTES_PER_SECTOR];
    fs_memset((uint8_t*)fs, 0, sizeof(HoneyFS));

    fs->partition_lba = FAT32_PARTITION_LBA;
    read_sectors(fs->partition_lba, 1, buffer);    /* Read the boot sector */

    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        return;    /* Invalid — leave fs->mounted = 0 */
    }

     /* Parse BPB fields from the boot sector (all little-endian) */
    fs->bytes_per_sector = rd16(&buffer[11]);    /* Should always be 512 */
    fs->sectors_per_cluster = buffer[13];        /* Power of 2; 8 = 4KB clusters */
    uint16_t reserved = rd16(&buffer[14]);       /* Sectors before FAT1 */
    fs->num_fats = buffer[16];                   /* Always 2 */
    fs->sectors_per_fat = rd32(&buffer[36]);     /* FAT32 extended BPB offset */
    fs->root_dir_cluster = rd32(&buffer[44]);    /* Root dir cluster, usually 2 */

    /* Sanity check — reject obviously corrupt or misformatted volumes */
    if (fs->bytes_per_sector != FAT32_BYTES_PER_SECTOR ||
        fs->sectors_per_cluster == 0 ||
        fs->num_fats == 0 ||
        fs->sectors_per_fat == 0 ||
        fs->root_dir_cluster < 2) {
        return;
    }

    /* Total sectors: BPB stores it in a 16-bit field for small disks, 
    or a 32-bit field for larger ones; FAT32 always uses the 32-bit field */
    uint32_t total_sectors = rd16(&buffer[19]);
    if (total_sectors == 0) total_sectors = rd32(&buffer[32]);

    /* Derive layout addresses from the parsed BPB fields */
    fs->fat_start_lba = fs->partition_lba + reserved;
    fs->data_start_lba = fs->fat_start_lba + (fs->num_fats * fs->sectors_per_fat);
    fs->total_clusters = (total_sectors - reserved -
        (fs->num_fats * fs->sectors_per_fat)) / fs->sectors_per_cluster;
    fs->next_free_cluster = 3;    /* Cluster 0 and 1 are reserved; data starts at 2 */
    fs->mounted = 1;    /* Volume is valid and ready to use */
}

/*
 * fs_find_entry — Search the root directory for a file by name.
 *
 * Reads the first sector of the root cluster, iterates through 16 directory
 * entries (16 * 32 bytes = 512 bytes = 1 sector), and compares 8.3 names.
 *
 * Skips: deleted entries (0xE5), end-of-dir markers (0x00),
 *        LFN entries (long filenames), directories, and volume labels.
 *
 * Returns FS_OK and fills out_entry on match; FS_ERR_NOT_FOUND otherwise.
 */

int fs_find_entry(HoneyFS *fs, const char *name, FAT32DirEntry *out_entry) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    uint8_t target[11];
    int name_result = to_fat_name(name, target);
    if (name_result != FS_OK) return name_result;
    if (read_root(fs, sector) != FS_OK) return FS_ERR_DISK;

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;

    for (int i = 0; i < 16; i++) {
        if (entries[i].name[0] == 0) break;    /* End of directory */
        if (entries[i].name[0] == FAT32_ENTRY_DELETED) continue;    /* Deleted entry */
        if ((entries[i].attributes & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;    /* Long filename */
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) continue;    /* Subdirectory */
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) continue;    /* Volume label */

         /* Compare 8 name bytes + 3 extension bytes */
        int match = 1;
        for (int j = 0; j < 8; j++) {
            if (entries[i].name[j] != target[j]) match = 0;
        }
        for (int j = 0; j < 3; j++) {
            if (entries[i].ext[j] != target[8 + j]) match = 0;
        }
        if (match) {
            *out_entry = entries[i];    /* Copy the matching entry to caller's buffer */
            return FS_OK;
        }
    }

    return FS_ERR_NOT_FOUND;
}

/*
 * find_entry_index — Like fs_find_entry, but returns the slot index (0-15)
 * and fills the caller's sector buffer with the raw root directory sector.
 * Used internally by fs_create, fs_write, and fs_delete to modify entries in-place.
 */
static int find_entry_index(HoneyFS *fs, const char *name, uint8_t *sector) {
    uint8_t target[11];
    int name_result = to_fat_name(name, target);
    if (name_result != FS_OK) return name_result;
    if (read_root(fs, sector) != FS_OK) return FS_ERR_DISK;

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;
    for (int i = 0; i < 16; i++) {
        if (entries[i].name[0] == 0) break;
        if (entries[i].name[0] == FAT32_ENTRY_DELETED) continue;

        int match = 1;
        for (int j = 0; j < 8; j++) {
            if (entries[i].name[j] != target[j]) match = 0;
        }
        for (int j = 0; j < 3; j++) {
            if (entries[i].ext[j] != target[8 + j]) match = 0;
        }
        if (match) return i;    /* Return slot index */
    }

    return FS_ERR_NOT_FOUND;
}

/*
 * fs_create — Create a new empty file in the root directory.
 *
 * Steps:
 *   1. Validate the filename and convert to 8.3 format
 *   2. Check that no file with this name already exists
 *   3. Find a free directory slot (deleted or never-used)
 *   4. Allocate one free cluster for the file (even empty files get a cluster)
 *   5. Write the directory entry and mark the cluster as EOF in the FAT
 */
int fs_create(HoneyFS *fs, const char *name) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    uint8_t fat_name[11];

    int name_result = to_fat_name(name, fat_name);
    if (name_result != FS_OK) return name_result;
    if (find_entry_index(fs, name, sector) >= 0) return FS_ERR_EXISTS;
    if (read_root(fs, sector) != FS_OK) return FS_ERR_DISK;

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;
    for (int i = 0; i < 16; i++) {
        /* Find a free slot: either never-used (0x00) or previously deleted (0xE5) */
        
        if (entries[i].name[0] == 0 || entries[i].name[0] == FAT32_ENTRY_DELETED) {
            int cluster = find_free_cluster(fs);
            if (cluster < 0) return FS_ERR_FULL;

            /* Zero out the entry and fill in the required fields */
            fs_memset((uint8_t*)&entries[i], 0, sizeof(FAT32DirEntry));
            fs_memcpy(entries[i].name, fat_name, 8);
            fs_memcpy(entries[i].ext, &fat_name[8], 3);
            entries[i].attributes = FAT32_ATTR_ARCHIVE;    /* Mark as a regular file */
            entries[i].file_size = 0;
            set_first_cluster(&entries[i], (uint32_t)cluster);

            /* Mark the allocated cluster as the end of its chain and zero it */
            fat_set(fs, (uint32_t)cluster, FAT32_EOC_MARK);
            clear_cluster(fs, (uint32_t)cluster);
            return write_root(fs, sector);    /* Persist the new directory entry */
        }
    }

    return FS_ERR_FULL;    /* All 16 root directory slots are occupied */
}

/*
 * fs_write — Write (overwrite) content to a file on disk.
 *
 * If the file doesn't exist yet, it is created first.
 * The old cluster chain is freed before a new one is allocated,
 * since FAT32 has no append mode in this version of HoneyOS.
 *
 * For each cluster needed:
 *   1. Allocate a free cluster
 *   2. Link it into the chain (set previous cluster's FAT entry to point here)
 *   3. Mark it EOF in the FAT
 *   4. Write sectors of content into it
 */
int fs_write(HoneyFS *fs, const char *name, const char *content) {
    uint8_t root_sector[FAT32_BYTES_PER_SECTOR];
    int idx = find_entry_index(fs, name, root_sector);

    /* Auto-create the file if it doesn't exist */
    if (idx == FS_ERR_NOT_FOUND) {
        int create_result = fs_create(fs, name);
        if (create_result != FS_OK) return create_result;
        idx = find_entry_index(fs, name, root_sector);
    }
    if (idx < 0) return idx;

    int len = content_len(content);
    if (len > FS_MAX_FILESIZE) return FS_ERR_SIZE;

    FAT32DirEntry *entries = (FAT32DirEntry*)root_sector;
    FAT32DirEntry *entry = &entries[idx];

    /* Free the existing cluster chain before writing new content */
    uint32_t old_cluster = first_cluster(entry);
    if (old_cluster >= 2) free_chain(fs, old_cluster);

    /* Calculate how many clusters the content needs */
    uint32_t cluster_size = fs->sectors_per_cluster * FAT32_BYTES_PER_SECTOR;
    uint32_t clusters_needed = (len == 0) ? 1 : ((uint32_t)len + cluster_size - 1) / cluster_size;
    uint32_t first = 0;    /* First cluster of the new chain */
    uint32_t previous = 0;    /* Used to link each cluster to the next */
    uint32_t written = 0;    /* Bytes written so far */

    for (uint32_t c = 0; c < clusters_needed; c++) {
        int allocated = find_free_cluster(fs);
        if (allocated < 0) return FS_ERR_FULL;

        uint32_t current = (uint32_t)allocated;
        if (first == 0) first = current;    /* Save head of chain */
        if (previous != 0) fat_set(fs, previous, current);    /* Link previous → current */
        fat_set(fs, current, FAT32_EOC_MARK);    /* Mark current as end of chain */
        clear_cluster(fs, current);    /* Zero the cluster before writing */

        /* Write content into this cluster, sector by sector */
        uint8_t sector[FAT32_BYTES_PER_SECTOR];
        uint32_t lba = fs_cluster_to_lba(fs, current);
        for (uint8_t s = 0; s < fs->sectors_per_cluster && written < (uint32_t)len; s++) {
            fs_memset(sector, 0, FAT32_BYTES_PER_SECTOR);
            uint32_t copy = (uint32_t)len - written;
            if (copy > FAT32_BYTES_PER_SECTOR) copy = FAT32_BYTES_PER_SECTOR;
            fs_memcpy(sector, (const uint8_t*)content + written, copy);
            write_sectors(lba + s, 1, sector);
            written += copy;
        }

        previous = current;
    }

    /* Update the directory entry with the new chain start and file size */
    set_first_cluster(entry, first);
    entry->file_size = (uint32_t)len;
    return write_root(fs, root_sector);
}

/*
 * fs_read — Read a file's content from disk into a caller-supplied buffer.
 *
 * Follows the FAT cluster chain starting from the file's first cluster,
 * reading each cluster's sectors until file_size bytes have been copied.
 * Always null-terminates the output buffer.
 */
int fs_read(HoneyFS *fs, const char *name, char *out_buf) {
    FAT32DirEntry entry;
    int result = fs_find_entry(fs, name, &entry);
    if (result != FS_OK) return result;

    uint32_t cluster = first_cluster(&entry);
    uint32_t remaining = entry.file_size;
    uint32_t copied = 0;

    if (remaining > FS_MAX_FILESIZE) return FS_ERR_SIZE;

    /* Handle empty file */
    if (cluster < 2) {
        out_buf[0] = '\0';
        return FS_OK;
    }

    /* Walk the cluster chain */
    while (remaining > 0 && cluster >= 2 && cluster < FAT32_BAD && !is_eoc(cluster)) {
        uint8_t sector[FAT32_BYTES_PER_SECTOR];
        uint32_t lba = fs_cluster_to_lba(fs, cluster);

        for (uint8_t s = 0; s < fs->sectors_per_cluster && remaining > 0; s++) {
            read_sectors(lba + s, 1, sector);
            uint32_t copy = remaining;
            if (copy > FAT32_BYTES_PER_SECTOR) copy = FAT32_BYTES_PER_SECTOR;
            fs_memcpy((uint8_t*)out_buf + copied, sector, copy);
            copied += copy;
            remaining -= copy;
        }

        /* Only advance to next cluster if there's still more to read */
        if (remaining > 0) cluster = fat_get(fs, cluster);
    }

    out_buf[copied] = '\0';    /* Null-terminate the output */
    return FS_OK;
}

/*
 * fs_delete — Delete a file from the root directory.
 *
 * Steps:
 *   1. Find the file's directory entry index
 *   2. Free its entire FAT cluster chain
 *   3. Mark the directory entry as deleted (0xE5 in first byte of name)
 *   4. Zero out file_size and cluster pointer in the entry
 *   5. Write the modified root directory back to disk
 */
int fs_delete(HoneyFS *fs, const char *name) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    int idx = find_entry_index(fs, name, sector);
    if (idx < 0) return idx;    /* Not found or invalid name */

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;
    uint32_t cluster = first_cluster(&entries[idx]);
    if (cluster >= 2) free_chain(fs, cluster);    /* Release disk space */

    entries[idx].name[0] = FAT32_ENTRY_DELETED;    /* 0xE5 = deleted marker */
    entries[idx].file_size = 0;
    set_first_cluster(&entries[idx], 0);
    return write_root(fs, sector);
}

/*
 * fs_list — Print all valid files in the root directory to the VGA screen.
 *
 * Reads the root directory sector and prints each non-deleted, non-system
 * entry in "NAME.EXT" format. Skips LFN entries, subdirectories, and labels.
 * Prints "(no files)" if the directory is empty.
 */
void fs_list(HoneyFS *fs) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    if (read_root(fs, sector) != FS_OK) {
        println("  (FAT32 volume not mounted)", 0x0C);
        return;
    }

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;
    int found = 0;
    int start_x = ui_get_cursor_x();
    int row = ui_get_cursor_y();

    for (int i = 0; i < 16; i++) {
        if (entries[i].name[0] == 0) break;    /* End of directory */
        if (entries[i].name[0] == FAT32_ENTRY_DELETED) continue;    /* Deleted */
        if ((entries[i].attributes & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;    /* LFN */
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) continue;    /* Subdirectory */
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) continue;    /* Volume label */

        /* Print the 8-char name (skip trailing spaces) */
        ui_set_cursor(start_x, row);
        print("  ", 0x0F);
        for (int j = 0; j < 8; j++) {
            if (entries[i].name[j] != ' ') vga_putchar(entries[i].name[j], 0x0A);
        }
        /* Print the 3-char extension if present */
        if (entries[i].ext[0] != ' ') {
            print(".", 0x0F);
            for (int j = 0; j < 3; j++) {
                if (entries[i].ext[j] != ' ') vga_putchar(entries[i].ext[j], 0x0A);
            }
        }
        println("", 0x0F);
        row++;
        found = 1;
    }

    if (!found) {
        ui_set_cursor(start_x, row);
        println("  (no files)", 0x0F);
    }
}
