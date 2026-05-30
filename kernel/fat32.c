/* ============================================================
 * FAT32 Filesystem Driver - fat32.c
 * HoneyOS | CMSC 125 - Phase 2
 *
 * Full FAT32 implementation:
 *   - BPB + FSInfo sector writing
 *   - 32-bit FAT table management
 *   - Root directory cluster chain
 *   - File create / write / read / delete
 *   - Cluster allocation with FSInfo cache updates
 * ============================================================ */

#include "fat32.h"
#include "ata.h"

/* ── Global FAT32 state (in RAM) ── */
static FAT32State fat32_state;    /* Single global instance; all fat32_ functions operate on this */

/* ============================================================
 * INTERNAL HELPERS - No stdlib in bare metal
 * ============================================================ */

/* f32_memset — Fill n bytes at dst with val (no stdlib memset available in bare metal) */
static void f32_memset(unsigned char *dst, unsigned char val, unsigned int n) {
    for (unsigned int i = 0; i < n; i++) dst[i] = val;
}

/* f32_memcpy — Copy n bytes from src to dst (no stdlib memcpy available) */
static void f32_memcpy(unsigned char *dst, const unsigned char *src,
                       unsigned int n) {
    for (unsigned int i = 0; i < n; i++) dst[i] = src[i];
}

/* f32_memcmp — Compare n bytes; returns 0 if equal, nonzero on first difference */
static int f32_memcmp(const unsigned char *a, const unsigned char *b,
                      unsigned int n) {
    for (unsigned int i = 0; i < n; i++)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}

/* f32_strlen — Return string length (no stdlib strlen available) */
static unsigned int f32_strlen(const char *s) {
    unsigned int i = 0;
    while (s[i]) i++;
    return i;
}

/* f32_toupper — Convert lowercase ASCII letter to uppercase for 8.3 name normalization */
static char f32_toupper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/* ============================================================
 * fat32_format_name
 * "hello.txt" → name[8]="HELLO   " ext[3]="TXT"
 * FAT32 short names (8.3) are uppercase, space-padded.
 * ============================================================ */
void fat32_format_name(const char *in,
                       unsigned char *name,
                       unsigned char *ext) {
    /* Space-pad both fields */
    for (int i = 0; i < 8; i++) name[i] = ' ';
    for (int i = 0; i < 3; i++) ext[i]  = ' ';

    int i = 0, ni = 0;

    /* Copy name part up to '.' */
    while (in[i] && in[i] != '.' && ni < 8)
        name[ni++] = (unsigned char)f32_toupper(in[i++]);

    /* Skip dot */
    if (in[i] == '.') i++;

    /* Copy extension */
    int ei = 0;
    while (in[i] && ei < 3)
        ext[ei++] = (unsigned char)f32_toupper(in[i++]);
}

/* ============================================================
 * fat32_unformat_name
 * name[8]="HELLO   " ext[3]="TXT" → "HELLO.TXT"
 * ============================================================ */
void fat32_unformat_name(const unsigned char *name,
                          const unsigned char *ext,
                          char *out) {
    int i = 0, oi = 0;

    while (i < 8 && name[i] != ' ')
        out[oi++] = (char)name[i++];

    out[oi++] = '.';
    i = 0;
    while (i < 3 && ext[i] != ' ')
        out[oi++] = (char)ext[i++];

    out[oi] = '\0';
}

/* ============================================================
 * fat32_get_entry
 * Read a 32-bit FAT entry for a cluster.
 * Top 4 bits are reserved and must be masked off.
 * ============================================================ */
unsigned int fat32_get_entry(unsigned int cluster) {
    return fat32_state.fat_table[cluster] & FAT32_CLUSTER_MASK;
}

/* ============================================================
 * fat32_set_entry
 * Write a 32-bit FAT entry.
 * Preserves the top 4 reserved bits of the existing entry.
 * ============================================================ */
void fat32_set_entry(unsigned int cluster, unsigned int value) {
    /* Preserve top 4 bits, write lower 28 */
    fat32_state.fat_table[cluster] =
        (fat32_state.fat_table[cluster] & 0xF0000000) |
        (value & FAT32_CLUSTER_MASK);
}

/* ============================================================
 * fat32_find_free_cluster
 * Scan the FAT for a free cluster.
 * Uses next_free_hint to start scan from last allocation point.
 * Returns cluster number or -1 if disk is full.
 * ============================================================ */
int fat32_find_free_cluster() {
    unsigned int start = fat32_state.next_free_hint;
    if (start < FAT32_MIN_DATA) start = FAT32_MIN_DATA;

    unsigned int total = FAT32_TOTAL_SECTORS / FAT32_SECTORS_PER_CLUSTER;

    /* Search from hint to end */
    for (unsigned int i = start; i < total; i++) {
        if (fat32_get_entry(i) == FAT32_CLUSTER_FREE) {
            fat32_state.next_free_hint = i + 1;
            if (fat32_state.free_count > 0)
                fat32_state.free_count--;
            return (int)i;
        }
    }

    /* Wrap around: search from start to hint */
    for (unsigned int i = FAT32_MIN_DATA; i < start; i++) {
        if (fat32_get_entry(i) == FAT32_CLUSTER_FREE) {
            fat32_state.next_free_hint = i + 1;
            if (fat32_state.free_count > 0)
                fat32_state.free_count--;
            return (int)i;
        }
    }

    return -1; /* Disk full */
}

/* ============================================================
 * fat32_find_entry
 * Search root directory cache for a file by 8.3 name.
 * Returns index into root_dir[] or FAT32_ERR_NOT_FOUND.
 * ============================================================ */
int fat32_find_entry(const char *name) {
    unsigned char fname[8], fext[3];
    fat32_format_name(name, fname, fext);

    for (int i = 0; i < FAT32_ROOT_CACHE_ENTRIES; i++) {
        FAT32DirEntry *e = &fat32_state.root_dir[i];

        /* Free = end of directory */
        if (e->name[0] == FAT32_ENTRY_FREE)    continue;
        if (e->name[0] == FAT32_ENTRY_DELETED)  continue;

        /* Skip LFN, directories, volume labels */
        if ((e->attributes & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;
        if (e->attributes & FAT32_ATTR_DIRECTORY) continue;
        if (e->attributes & FAT32_ATTR_VOLUME_ID) continue;

        /* Compare 8.3 name */
        if (f32_memcmp(e->name, fname, 8) == 0 &&
            f32_memcmp(e->ext,  fext,  3) == 0) {
            return i;
        }
    }
    return FAT32_ERR_NOT_FOUND;
}

/* ============================================================
 * fat32_cluster_to_lba
 * Convert a cluster number to its first sector (LBA address).
 * Formula: data_start + (cluster - 2) * sectors_per_cluster
 * ============================================================ */
static unsigned int fat32_cluster_to_lba(unsigned int cluster) {
    return FAT32_DATA_START +
           (cluster - FAT32_MIN_DATA) * FAT32_SECTORS_PER_CLUSTER;
}

/* ============================================================
 * fat32_flush_fat
 * Write FAT table from RAM back to disk (both FAT1 and FAT2).
 * Called after any cluster allocation or deallocation.
 * ============================================================ */
int fat32_flush_fat() {
    int ret;
    unsigned char *fat_bytes = (unsigned char*)fat32_state.fat_table;

    /* Write FAT1 */
    ret = ata_write_sectors(FAT32_FAT1_START,
                            (unsigned char)FAT32_SECTORS_PER_FAT,
                            fat_bytes);
    if (ret != ATA_OK) return FAT32_ERR_DISK;

    /* Write FAT2 (backup) */
    ret = ata_write_sectors(FAT32_FAT2_START,
                            (unsigned char)FAT32_SECTORS_PER_FAT,
                            fat_bytes);
    if (ret != ATA_OK) return FAT32_ERR_DISK;

    return FAT32_OK;
}

/* ============================================================
 * fat32_flush_root
 * Write root directory cache back to its cluster on disk.
 * Root directory lives in cluster chain starting at cluster 2.
 * ============================================================ */
int fat32_flush_root() {
    unsigned int lba = fat32_cluster_to_lba(FAT32_ROOT_CLUSTER);
    unsigned char *root_bytes = (unsigned char*)fat32_state.root_dir;

    /* Root dir fits in first cluster (128 entries * 32 bytes = 4096 bytes
     * = 8 sectors = 1 cluster for our geometry) */
    unsigned int root_bytes_size = FAT32_ROOT_CACHE_ENTRIES *
                                   sizeof(FAT32DirEntry);
    unsigned int sectors_needed  = root_bytes_size / FAT32_BYTES_PER_SECTOR;
    if (sectors_needed == 0) sectors_needed = 1;

    int ret = ata_write_sectors(lba,
                                (unsigned char)sectors_needed,
                                root_bytes);
    return (ret == ATA_OK) ? FAT32_OK : FAT32_ERR_DISK;
}

/* ============================================================
 * fat32_flush_fsinfo
 * Write FSInfo sector back to disk with updated free count.
 * ============================================================ */
static int fat32_flush_fsinfo() {
    unsigned char sector[FAT32_BYTES_PER_SECTOR];
    f32_memset(sector, 0, FAT32_BYTES_PER_SECTOR);

    FAT32FSInfo *info = (FAT32FSInfo*)sector;
    info->signature1       = FAT32_FSINFO_SIG1;
    info->signature2       = FAT32_FSINFO_SIG2;
    info->free_count       = fat32_state.free_count;
    info->next_free        = fat32_state.next_free_hint;
    info->trail_signature  = FAT32_FSINFO_SIG3;

    int ret = ata_write_sector(FAT32_FSINFO_SECTOR, sector);
    return (ret == ATA_OK) ? FAT32_OK : FAT32_ERR_DISK;
}

/* ============================================================
 * fat32_write_bpb
 * Write the FAT32 Boot Sector (BPB) to sector 0 and sector 6.
 * This is the filesystem "header" read by bootloaders.
 * ============================================================ */
static int fat32_write_bpb() {
    unsigned char sector[FAT32_BYTES_PER_SECTOR];
    f32_memset(sector, 0, FAT32_BYTES_PER_SECTOR);

    FAT32BPB *bpb = (FAT32BPB*)sector;

    /* Boot jump: EB 58 90 */
    bpb->jump[0] = 0xEB;
    bpb->jump[1] = 0x58;
    bpb->jump[2] = 0x90;

    /* OEM name */
    unsigned char oem[] = "HoneyOS ";
    f32_memcpy(bpb->oem_name, oem, 8);

    /* Standard BPB */
    bpb->bytes_per_sector    = FAT32_BYTES_PER_SECTOR;
    bpb->sectors_per_cluster = FAT32_SECTORS_PER_CLUSTER;
    bpb->reserved_sectors    = FAT32_RESERVED_SECTORS;
    bpb->num_fats            = FAT32_NUM_FATS;
    bpb->root_entry_count    = 0;    /* Must be 0 for FAT32          */
    bpb->total_sectors_16    = 0;    /* Must be 0 for FAT32          */
    bpb->media_descriptor    = FAT32_MEDIA_DESCRIPTOR;
    bpb->sectors_per_fat_16  = 0;    /* Must be 0 for FAT32          */
    bpb->sectors_per_track   = FAT32_SECTORS_PER_TRACK;
    bpb->num_heads           = FAT32_NUM_HEADS;
    bpb->hidden_sectors      = 0;
    bpb->total_sectors_32    = FAT32_TOTAL_SECTORS;

    /* FAT32 Extended BPB */
    bpb->sectors_per_fat_32  = FAT32_SECTORS_PER_FAT;
    bpb->ext_flags           = 0;       /* Mirror FAT to all copies    */
    bpb->fs_version          = 0x0000;  /* FAT32 version 0.0           */
    bpb->root_cluster        = FAT32_ROOT_CLUSTER;
    bpb->fs_info_sector      = FAT32_FSINFO_SECTOR;
    bpb->backup_boot_sector  = FAT32_BACKUP_BOOT_SECTOR;

    /* Extended boot record */
    bpb->drive_number   = 0x80;
    bpb->boot_signature = 0x29;
    bpb->volume_id      = 0xDEADBEEF;

    unsigned char label[] = "HONEYOS    ";
    f32_memcpy(bpb->volume_label, label, 11);

    unsigned char fstype[] = "FAT32   ";
    f32_memcpy(bpb->fs_type, fstype, 8);

    /* Boot signature */
    sector[510] = 0x55;
    sector[511] = 0xAA;

    /* Write primary boot sector */
    int ret = ata_write_sector(0, sector);
    if (ret != ATA_OK) return FAT32_ERR_DISK;

    /* Write backup boot sector at sector 6 */
    ret = ata_write_sector(FAT32_BACKUP_BOOT_SECTOR, sector);
    if (ret != ATA_OK) return FAT32_ERR_DISK;

    return FAT32_OK;
}

/* ============================================================
 * fat32_init
 * Format the virtual disk with FAT32 and load state into RAM.
 *
 * Steps:
 *   1.  Init ATA disk driver
 *   2.  Write BPB (boot + backup)
 *   3.  Initialize FAT table in RAM
 *       - Cluster 0: 0x0FFFFFF8 (media type + reserved)
 *       - Cluster 1: 0x0FFFFFFF (end-of-chain)
 *       - Cluster 2: 0x0FFFFFFF (root dir, single cluster)
 *   4.  Write FSInfo sector
 *   5.  Clear root directory in RAM
 *   6.  Flush FAT + root dir to disk
 * ============================================================ */
int fat32_init() {
    int ret;

    /* Step 1: Init ATA */
    ret = ata_init();
    if (ret != ATA_OK) return FAT32_ERR_DISK;

    /* Step 2: Write BPB */
    ret = fat32_write_bpb();
    if (ret != FAT32_OK) return ret;

    /* Step 3: Initialize FAT in RAM */
    unsigned int fat_entries = FAT32_SECTORS_PER_FAT *
                               FAT32_BYTES_PER_SECTOR / 4;
    for (unsigned int i = 0; i < fat_entries; i++)
        fat32_state.fat_table[i] = FAT32_CLUSTER_FREE;

    /* Reserved entries — FAT32 spec requires these exact values in clusters 0-2 */
    fat32_state.fat_table[0] = 0x0FFFFFF8;             /* Media type (lower byte = 0xF8 = fixed disk) */
    fat32_state.fat_table[1] = FAT32_CLUSTER_EOF;      /* End-of-chain marker, always set       */
    fat32_state.fat_table[2] = FAT32_CLUSTER_EOF;      /* Root directory occupies cluster 2     */

    /* Init FSInfo cache */
    fat32_state.free_count      = fat_entries - 3;
    fat32_state.next_free_hint  = 3;

    /* Step 4: Write FSInfo */
    ret = fat32_flush_fsinfo();
    if (ret != FAT32_OK) return ret;

    /* Step 5: Clear root directory in RAM */
    f32_memset((unsigned char*)fat32_state.root_dir,
               0,
               sizeof(fat32_state.root_dir));

    /* Step 6: Flush FAT + root dir */
    ret = fat32_flush_fat();
    if (ret != FAT32_OK) return ret;

    ret = fat32_flush_root();
    if (ret != FAT32_OK) return ret;

    fat32_state.initialized = 1;
    return FAT32_OK;
}

/* ============================================================
 * fat32_create
 * Create a new empty file in the root directory.
 *
 * Steps:
 *   1. Validate and format 8.3 name
 *   2. Check for duplicate
 *   3. Find a free root directory slot
 *   4. Fill directory entry (no clusters yet, size = 0)
 *   5. Flush root dir to disk
 * ============================================================ */
int fat32_create(const char *name) {
    if (!fat32_state.initialized) return FAT32_ERR_NOINIT;

    /* Check file already exists */
    if (fat32_find_entry(name) >= 0) return FAT32_ERR_EXISTS;

    /* Format 8.3 name */
    unsigned char fname[8], fext[3];
    fat32_format_name(name, fname, fext);

    /* Find free slot in root dir */
    for (int i = 0; i < FAT32_ROOT_CACHE_ENTRIES; i++) {
        FAT32DirEntry *e = &fat32_state.root_dir[i];

        if (e->name[0] == FAT32_ENTRY_FREE ||
            e->name[0] == FAT32_ENTRY_DELETED) {

            /* Zero the entry */
            f32_memset((unsigned char*)e, 0, sizeof(FAT32DirEntry));

            /* Set name and attributes */
            f32_memcpy(e->name, fname, 8);
            f32_memcpy(e->ext,  fext,  3);
            e->attributes    = FAT32_ATTR_ARCHIVE;
            e->file_size     = 0;

            /* No cluster yet — first_cluster = 0 means empty file */
            FAT32_SET_CLUSTER(e, 0);

            return fat32_flush_root();
        }
    }

    return FAT32_ERR_FULL;
}

/* ============================================================
 * fat32_free_chain
 * Free an entire cluster chain in the FAT.
 * Used by write (to replace content) and delete.
 * ============================================================ */
static void fat32_free_chain(unsigned int start_cluster) {
    unsigned int cluster = start_cluster;

    while (cluster >= FAT32_MIN_DATA &&
           cluster < FAT32_CLUSTER_BAD) {
        unsigned int next = fat32_get_entry(cluster);
        fat32_set_entry(cluster, FAT32_CLUSTER_FREE);
        fat32_state.free_count++;
        cluster = next;
    }
}

/* ============================================================
 * fat32_write
 * Write data to an existing file.
 *
 * Steps:
 *   1. Find directory entry
 *   2. Free existing cluster chain
 *   3. Allocate new clusters, build chain in FAT
 *   4. Write data sector by sector per cluster
 *   5. Update directory entry (first cluster + size)
 *   6. Flush FAT + root dir + FSInfo
 * ============================================================ */
int fat32_write(const char *name, const char *data, unsigned int size) {
    if (!fat32_state.initialized) return FAT32_ERR_NOINIT;
    if (size > FAT32_MAX_FILESIZE)  return FAT32_ERR_SIZE;

    /* Find the directory entry */
    int idx = fat32_find_entry(name);
    if (idx < 0) return FAT32_ERR_NOT_FOUND;

    FAT32DirEntry *entry = &fat32_state.root_dir[idx];

    /* Step 2: Free existing cluster chain */
    unsigned int old_start = FAT32_FIRST_CLUSTER(entry);
    if (old_start >= FAT32_MIN_DATA)
        fat32_free_chain(old_start);

    /* Handle empty write */
    if (size == 0) {
        FAT32_SET_CLUSTER(entry, 0);
        entry->file_size = 0;
        fat32_flush_fat();
        fat32_flush_root();
        fat32_flush_fsinfo();
        return FAT32_OK;
    }

    /* Step 3: Allocate clusters for new data */
    unsigned int  first_cluster = 0;
    unsigned int  prev_cluster  = 0;
    unsigned int  remaining     = size;
    const unsigned char *src    = (const unsigned char*)data;
    unsigned char sector_buf[FAT32_BYTES_PER_SECTOR];

    /* How many clusters do we need? */
    unsigned int cluster_size = FAT32_SECTORS_PER_CLUSTER *
                                FAT32_BYTES_PER_SECTOR;
    unsigned int clusters_needed = (size + cluster_size - 1) / cluster_size;

    for (unsigned int c = 0; c < clusters_needed; c++) {
        /* Find free cluster */
        int new_clus = fat32_find_free_cluster();
        if (new_clus < 0) return FAT32_ERR_FULL;

        /* Mark as EOF for now */
        fat32_set_entry((unsigned int)new_clus, FAT32_CLUSTER_EOF);

        /* Link previous cluster */
        if (prev_cluster != 0)
            fat32_set_entry(prev_cluster, (unsigned int)new_clus);
        else
            first_cluster = (unsigned int)new_clus;

        /* Step 4: Write data sectors for this cluster */
        unsigned int lba = fat32_cluster_to_lba((unsigned int)new_clus);

        for (unsigned int s = 0;
             s < FAT32_SECTORS_PER_CLUSTER && remaining > 0;
             s++) {

            f32_memset(sector_buf, 0, FAT32_BYTES_PER_SECTOR);

            unsigned int to_copy = remaining;
            if (to_copy > FAT32_BYTES_PER_SECTOR)
                to_copy = FAT32_BYTES_PER_SECTOR;

            f32_memcpy(sector_buf, src, to_copy);
            src       += to_copy;
            remaining -= to_copy;

            int ret = ata_write_sector(lba + s, sector_buf);
            if (ret != ATA_OK) return FAT32_ERR_DISK;
        }

        prev_cluster = (unsigned int)new_clus;
    }

    /* Step 5: Update directory entry */
    FAT32_SET_CLUSTER(entry, first_cluster);
    entry->file_size = size;

    /* Step 6: Flush everything */
    int ret;
    ret = fat32_flush_fat();   if (ret != FAT32_OK) return ret;
    ret = fat32_flush_root();  if (ret != FAT32_OK) return ret;
    ret = fat32_flush_fsinfo();
    return ret;
}

/* ============================================================
 * fat32_read
 * Read a file's contents by following the cluster chain.
 *
 * Steps:
 *   1. Find directory entry → get first cluster + size
 *   2. Follow cluster chain in FAT
 *   3. Read each cluster's sectors into buf
 *   4. Null-terminate
 * ============================================================ */
int fat32_read(const char *name, char *buf) {
    if (!fat32_state.initialized) return FAT32_ERR_NOINIT;

    /* Find directory entry */
    int idx = fat32_find_entry(name);
    if (idx < 0) return FAT32_ERR_NOT_FOUND;

    FAT32DirEntry *entry   = &fat32_state.root_dir[idx];
    unsigned int   cluster = FAT32_FIRST_CLUSTER(entry);
    unsigned int   remaining = entry->file_size;
    unsigned char *dst     = (unsigned char*)buf;
    unsigned char  sector_buf[FAT32_BYTES_PER_SECTOR];

    /* Empty file */
    if (remaining == 0 || cluster < FAT32_MIN_DATA) {
        buf[0] = '\0';
        return FAT32_OK;
    }

    /* Step 2-3: Follow cluster chain */
    while (cluster >= FAT32_MIN_DATA &&
           cluster < FAT32_CLUSTER_BAD &&
           remaining > 0) {

        unsigned int lba = fat32_cluster_to_lba(cluster);

        /* Read each sector in this cluster */
        for (unsigned int s = 0;
             s < FAT32_SECTORS_PER_CLUSTER && remaining > 0;
             s++) {

            int ret = ata_read_sector(lba + s, sector_buf);
            if (ret != ATA_OK) return FAT32_ERR_DISK;

            unsigned int to_copy = remaining;
            if (to_copy > FAT32_BYTES_PER_SECTOR)
                to_copy = FAT32_BYTES_PER_SECTOR;

            f32_memcpy(dst, sector_buf, to_copy);
            dst       += to_copy;
            remaining -= to_copy;
        }

        /* Next cluster in chain */
        cluster = fat32_get_entry(cluster);
    }

    /* Step 4: Null-terminate */
    *dst = '\0';
    return FAT32_OK;
}

/* ============================================================
 * fat32_delete
 * Delete a file:
 *   1. Find and mark directory entry as deleted (0xE5)
 *   2. Free all clusters in the FAT chain
 *   3. Flush FAT + root dir + FSInfo
 * ============================================================ */
int fat32_delete(const char *name) {
    if (!fat32_state.initialized) return FAT32_ERR_NOINIT;

    /* Find directory entry */
    int idx = fat32_find_entry(name);
    if (idx < 0) return FAT32_ERR_NOT_FOUND;

    FAT32DirEntry *entry = &fat32_state.root_dir[idx];

    /* Step 2: Free cluster chain */
    unsigned int cluster = FAT32_FIRST_CLUSTER(entry);
    if (cluster >= FAT32_MIN_DATA)
        fat32_free_chain(cluster);

    /* Step 1: Mark entry as deleted */
    entry->name[0] = FAT32_ENTRY_DELETED;
    FAT32_SET_CLUSTER(entry, 0);
    entry->file_size = 0;

    /* Step 3: Flush */
    int ret;
    ret = fat32_flush_fat();   if (ret != FAT32_OK) return ret;
    ret = fat32_flush_root();  if (ret != FAT32_OK) return ret;
    ret = fat32_flush_fsinfo();
    return ret;
}

/* ============================================================
 * fat32_list
 * Fill out_entries with valid file entries from root dir.
 * Returns count of files found.
 * ============================================================ */
int fat32_list(FAT32DirEntry *out_entries, int max_entries) {
    if (!fat32_state.initialized) return 0;

    int count = 0;
    for (int i = 0;
         i < FAT32_ROOT_CACHE_ENTRIES && count < max_entries;
         i++) {
        FAT32DirEntry *e = &fat32_state.root_dir[i];

        if (e->name[0] == FAT32_ENTRY_FREE)     continue;
        if (e->name[0] == FAT32_ENTRY_DELETED)  continue;
        if ((e->attributes & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;
        if (e->attributes & FAT32_ATTR_DIRECTORY) continue;
        if (e->attributes & FAT32_ATTR_VOLUME_ID) continue;

        out_entries[count++] = *e;
    }

    return count;
}
