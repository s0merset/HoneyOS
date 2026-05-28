#include "../include/honeyfs.h"
#include "../include/disk.h"

#define FAT32_PARTITION_LBA     2048
#define FAT32_BYTES_PER_SECTOR  512
#define FAT32_EOC              0x0FFFFFF8
#define FAT32_EOC_MARK         0x0FFFFFFF
#define FAT32_FREE             0x00000000
#define FAT32_BAD              0x0FFFFFF7
#define FAT32_ATTR_ARCHIVE     0x20
#define FAT32_ATTR_DIRECTORY   0x10
#define FAT32_ATTR_VOLUME_ID   0x08
#define FAT32_ATTR_LFN         0x0F
#define FAT32_ENTRY_DELETED    0xE5

extern void print(const char *s, unsigned char color);
extern void vga_putchar(char c, unsigned char color);
extern void println(const char *s, unsigned char color);

static void fs_memset(uint8_t *dst, uint8_t value, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) dst[i] = value;
}

static void fs_memcpy(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) dst[i] = src[i];
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static char upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static int content_len(const char *content) {
    int len = 0;
    while (content[len] != '\0' && len <= FS_MAX_FILESIZE) len++;
    return len;
}

static int is_eoc(uint32_t cluster) {
    return cluster >= FAT32_EOC;
}

static uint32_t first_cluster(FAT32DirEntry *entry) {
    return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

static void set_first_cluster(FAT32DirEntry *entry, uint32_t cluster) {
    entry->cluster_high = (uint16_t)((cluster >> 16) & 0xFFFF);
    entry->cluster_low = (uint16_t)(cluster & 0xFFFF);
}

// Converts "file.txt" to "FILE    TXT" (8.3 short-name format).
static int to_fat_name(const char *name, uint8_t *fat_name) {
    for (int i = 0; i < 11; i++) fat_name[i] = ' ';

    int i = 0;
    int k = 0;
    while (name[i] != '.' && name[i] != '\0') {
        if (k >= 8 || name[i] == ' ') return FS_ERR_NAME;
        fat_name[k++] = (uint8_t)upper(name[i++]);
    }

    if (k == 0) return FS_ERR_NAME;

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

uint32_t fs_cluster_to_lba(HoneyFS *fs, uint32_t cluster) {
    return fs->data_start_lba + ((cluster - 2) * fs->sectors_per_cluster);
}

static uint32_t fat_sector_lba(HoneyFS *fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    return fs->fat_start_lba + (fat_offset / FAT32_BYTES_PER_SECTOR);
}

static int fat_entry_offset(uint32_t cluster) {
    return (int)((cluster * 4) % FAT32_BYTES_PER_SECTOR);
}

static uint32_t fat_get(HoneyFS *fs, uint32_t cluster) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    read_sectors(fat_sector_lba(fs, cluster), 1, sector);
    return rd32(&sector[fat_entry_offset(cluster)]) & 0x0FFFFFFF;
}

static void wr32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
}

static void fat_set(HoneyFS *fs, uint32_t cluster, uint32_t value) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];

    for (uint8_t fat = 0; fat < fs->num_fats; fat++) {
        uint32_t lba = fs->fat_start_lba +
            (fat * fs->sectors_per_fat) +
            ((cluster * 4) / FAT32_BYTES_PER_SECTOR);
        int offset = fat_entry_offset(cluster);

        read_sectors(lba, 1, sector);
        uint32_t old = rd32(&sector[offset]);
        wr32(&sector[offset], (old & 0xF0000000) | (value & 0x0FFFFFFF));
        write_sectors(lba, 1, sector);
    }
}

static int find_free_cluster(HoneyFS *fs) {
    uint32_t start = fs->next_free_cluster;
    if (start < 3 || start >= fs->total_clusters + 2) start = 3;

    for (uint32_t cluster = start; cluster < fs->total_clusters + 2; cluster++) {
        if (fat_get(fs, cluster) == FAT32_FREE) {
            fs->next_free_cluster = cluster + 1;
            return (int)cluster;
        }
    }

    for (uint32_t cluster = 3; cluster < start; cluster++) {
        if (fat_get(fs, cluster) == FAT32_FREE) {
            fs->next_free_cluster = cluster + 1;
            return (int)cluster;
        }
    }

    return FS_ERR_FULL;
}

static void clear_cluster(HoneyFS *fs, uint32_t cluster) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    fs_memset(sector, 0, FAT32_BYTES_PER_SECTOR);

    uint32_t lba = fs_cluster_to_lba(fs, cluster);
    for (uint8_t i = 0; i < fs->sectors_per_cluster; i++) {
        write_sectors(lba + i, 1, sector);
    }
}

static void free_chain(HoneyFS *fs, uint32_t cluster) {
    while (cluster >= 2 && cluster < FAT32_BAD && !is_eoc(cluster)) {
        uint32_t next = fat_get(fs, cluster);
        fat_set(fs, cluster, FAT32_FREE);
        cluster = next;
    }

    if (cluster >= 2 && cluster < FAT32_BAD) {
        fat_set(fs, cluster, FAT32_FREE);
    }
}

static int read_root(HoneyFS *fs, uint8_t *sector) {
    if (!fs->mounted) return FS_ERR_DISK;
    read_sectors(fs_cluster_to_lba(fs, fs->root_dir_cluster), 1, sector);
    return FS_OK;
}

static int write_root(HoneyFS *fs, uint8_t *sector) {
    if (!fs->mounted) return FS_ERR_DISK;
    write_sectors(fs_cluster_to_lba(fs, fs->root_dir_cluster), 1, sector);
    return FS_OK;
}

void fs_init(HoneyFS *fs) {
    uint8_t buffer[FAT32_BYTES_PER_SECTOR];
    fs_memset((uint8_t*)fs, 0, sizeof(HoneyFS));

    fs->partition_lba = FAT32_PARTITION_LBA;
    read_sectors(fs->partition_lba, 1, buffer);

    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        return;
    }

    fs->bytes_per_sector = rd16(&buffer[11]);
    fs->sectors_per_cluster = buffer[13];
    uint16_t reserved = rd16(&buffer[14]);
    fs->num_fats = buffer[16];
    fs->sectors_per_fat = rd32(&buffer[36]);
    fs->root_dir_cluster = rd32(&buffer[44]);

    if (fs->bytes_per_sector != FAT32_BYTES_PER_SECTOR ||
        fs->sectors_per_cluster == 0 ||
        fs->num_fats == 0 ||
        fs->sectors_per_fat == 0 ||
        fs->root_dir_cluster < 2) {
        return;
    }

    uint32_t total_sectors = rd16(&buffer[19]);
    if (total_sectors == 0) total_sectors = rd32(&buffer[32]);

    fs->fat_start_lba = fs->partition_lba + reserved;
    fs->data_start_lba = fs->fat_start_lba + (fs->num_fats * fs->sectors_per_fat);
    fs->total_clusters = (total_sectors - reserved -
        (fs->num_fats * fs->sectors_per_fat)) / fs->sectors_per_cluster;
    fs->next_free_cluster = 3;
    fs->mounted = 1;
}

int fs_find_entry(HoneyFS *fs, const char *name, FAT32DirEntry *out_entry) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    uint8_t target[11];
    int name_result = to_fat_name(name, target);
    if (name_result != FS_OK) return name_result;
    if (read_root(fs, sector) != FS_OK) return FS_ERR_DISK;

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;

    for (int i = 0; i < 16; i++) {
        if (entries[i].name[0] == 0) break;
        if (entries[i].name[0] == FAT32_ENTRY_DELETED) continue;
        if ((entries[i].attributes & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) continue;
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) continue;

        int match = 1;
        for (int j = 0; j < 8; j++) {
            if (entries[i].name[j] != target[j]) match = 0;
        }
        for (int j = 0; j < 3; j++) {
            if (entries[i].ext[j] != target[8 + j]) match = 0;
        }
        if (match) {
            *out_entry = entries[i];
            return FS_OK;
        }
    }

    return FS_ERR_NOT_FOUND;
}

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
        if (match) return i;
    }

    return FS_ERR_NOT_FOUND;
}

int fs_create(HoneyFS *fs, const char *name) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    uint8_t fat_name[11];

    int name_result = to_fat_name(name, fat_name);
    if (name_result != FS_OK) return name_result;
    if (find_entry_index(fs, name, sector) >= 0) return FS_ERR_EXISTS;
    if (read_root(fs, sector) != FS_OK) return FS_ERR_DISK;

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;
    for (int i = 0; i < 16; i++) {
        if (entries[i].name[0] == 0 || entries[i].name[0] == FAT32_ENTRY_DELETED) {
            int cluster = find_free_cluster(fs);
            if (cluster < 0) return FS_ERR_FULL;

            fs_memset((uint8_t*)&entries[i], 0, sizeof(FAT32DirEntry));
            fs_memcpy(entries[i].name, fat_name, 8);
            fs_memcpy(entries[i].ext, &fat_name[8], 3);
            entries[i].attributes = FAT32_ATTR_ARCHIVE;
            entries[i].file_size = 0;
            set_first_cluster(&entries[i], (uint32_t)cluster);

            fat_set(fs, (uint32_t)cluster, FAT32_EOC_MARK);
            clear_cluster(fs, (uint32_t)cluster);
            return write_root(fs, sector);
        }
    }

    return FS_ERR_FULL;
}

int fs_write(HoneyFS *fs, const char *name, const char *content) {
    uint8_t root_sector[FAT32_BYTES_PER_SECTOR];
    int idx = find_entry_index(fs, name, root_sector);
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
    uint32_t old_cluster = first_cluster(entry);
    if (old_cluster >= 2) free_chain(fs, old_cluster);

    uint32_t cluster_size = fs->sectors_per_cluster * FAT32_BYTES_PER_SECTOR;
    uint32_t clusters_needed = (len == 0) ? 1 : ((uint32_t)len + cluster_size - 1) / cluster_size;
    uint32_t first = 0;
    uint32_t previous = 0;
    uint32_t written = 0;

    for (uint32_t c = 0; c < clusters_needed; c++) {
        int allocated = find_free_cluster(fs);
        if (allocated < 0) return FS_ERR_FULL;

        uint32_t current = (uint32_t)allocated;
        if (first == 0) first = current;
        if (previous != 0) fat_set(fs, previous, current);
        fat_set(fs, current, FAT32_EOC_MARK);
        clear_cluster(fs, current);

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

    set_first_cluster(entry, first);
    entry->file_size = (uint32_t)len;
    return write_root(fs, root_sector);
}

int fs_read(HoneyFS *fs, const char *name, char *out_buf) {
    FAT32DirEntry entry;
    int result = fs_find_entry(fs, name, &entry);
    if (result != FS_OK) return result;

    uint32_t cluster = first_cluster(&entry);
    uint32_t remaining = entry.file_size;
    uint32_t copied = 0;

    if (remaining > FS_MAX_FILESIZE) return FS_ERR_SIZE;
    if (cluster < 2) {
        out_buf[0] = '\0';
        return FS_OK;
    }

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

        if (remaining > 0) cluster = fat_get(fs, cluster);
    }

    out_buf[copied] = '\0';
    return FS_OK;
}

int fs_delete(HoneyFS *fs, const char *name) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    int idx = find_entry_index(fs, name, sector);
    if (idx < 0) return idx;

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;
    uint32_t cluster = first_cluster(&entries[idx]);
    if (cluster >= 2) free_chain(fs, cluster);

    entries[idx].name[0] = FAT32_ENTRY_DELETED;
    entries[idx].file_size = 0;
    set_first_cluster(&entries[idx], 0);
    return write_root(fs, sector);
}

void fs_list(HoneyFS *fs) {
    uint8_t sector[FAT32_BYTES_PER_SECTOR];
    if (read_root(fs, sector) != FS_OK) {
        println("  (FAT32 volume not mounted)", 0x0C);
        return;
    }

    FAT32DirEntry *entries = (FAT32DirEntry*)sector;
    int found = 0;

    for (int i = 0; i < 16; i++) {
        if (entries[i].name[0] == 0) break;
        if (entries[i].name[0] == FAT32_ENTRY_DELETED) continue;
        if ((entries[i].attributes & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) continue;
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) continue;

        print("  ", 0x0F);
        for (int j = 0; j < 8; j++) {
            if (entries[i].name[j] != ' ') vga_putchar(entries[i].name[j], 0x0A);
        }
        if (entries[i].ext[0] != ' ') {
            print(".", 0x0F);
            for (int j = 0; j < 3; j++) {
                if (entries[i].ext[j] != ' ') vga_putchar(entries[i].ext[j], 0x0A);
            }
        }
        println("", 0x0F);
        found = 1;
    }

    if (!found) println("  (no files)", 0x0F);
}
