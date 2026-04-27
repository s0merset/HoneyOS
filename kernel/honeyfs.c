/* ============================================================
 * HoneyFS - Simple In-Memory Filesystem
 * honeyfs.c
 * ============================================================
 */

#include "../include/honeyfs.h"

/* ── String helpers (no stdlib in bare metal) ── */

static int str_len(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void str_cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void mem_set(char *dst, char val, int n) {
    for (int i = 0; i < n; i++) dst[i] = val;
}

/* ── Check if filename ends with .txt ── */
static int is_txt(const char *name) {
    int len = str_len(name);
    if (len < 5) return 0; /* minimum: "a.txt" */
    return (name[len-4] == '.' &&
            name[len-3] == 't' &&
            name[len-2] == 'x' &&
            name[len-1] == 't');
}

/* ============================================================
 * fs_init - Initialize the filesystem (wipe all slots)
 * ============================================================ */
void fs_init(HoneyFS *fs) {
    fs->file_count = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs->files[i].status = FS_SLOT_FREE;
        fs->files[i].size   = 0;
        mem_set(fs->files[i].name,    0, FS_MAX_FILENAME);
        mem_set(fs->files[i].content, 0, FS_MAX_FILESIZE);
    }
}

/* ============================================================
 * fs_find - Find index of a file by name, -1 if not found
 * ============================================================ */
int fs_find(HoneyFS *fs, const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (fs->files[i].status == FS_SLOT_USED &&
            str_cmp(fs->files[i].name, name) == 0) {
            return i;
        }
    }
    return FS_ERR_NOT_FOUND;
}

/* ============================================================
 * fs_create - Create a new empty .txt file
 * Returns: FS_OK or error code
 * ============================================================ */
int fs_create(HoneyFS *fs, const char *name) {
    /* Validate .txt extension */
    if (!is_txt(name)) return FS_ERR_NAME;

    /* Check if already exists */
    if (fs_find(fs, name) >= 0) return FS_ERR_EXISTS;

    /* Check if filesystem is full */
    if (fs->file_count >= FS_MAX_FILES) return FS_ERR_FULL;

    /* Find a free slot */
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (fs->files[i].status == FS_SLOT_FREE) {
            fs->files[i].status = FS_SLOT_USED;
            str_cpy(fs->files[i].name, name, FS_MAX_FILENAME);
            fs->files[i].size = 0;
            mem_set(fs->files[i].content, 0, FS_MAX_FILESIZE);
            fs->file_count++;
            return FS_OK;
        }
    }

    return FS_ERR_FULL;
}

/* ============================================================
 * fs_write - Write content to an existing file (overwrites)
 * Returns: FS_OK or error code
 * ============================================================ */
int fs_write(HoneyFS *fs, const char *name, const char *content) {
    int idx = fs_find(fs, name);
    if (idx < 0) return FS_ERR_NOT_FOUND;

    int len = str_len(content);
    if (len >= FS_MAX_FILESIZE) return FS_ERR_SIZE;

    str_cpy(fs->files[idx].content, content, FS_MAX_FILESIZE);
    fs->files[idx].size = len;

    return FS_OK;
}

/* ============================================================
 * fs_read - Read content of a file into out_buf
 * Returns: FS_OK or error code
 * ============================================================ */
int fs_read(HoneyFS *fs, const char *name, char *out_buf) {
    int idx = fs_find(fs, name);
    if (idx < 0) return FS_ERR_NOT_FOUND;

    str_cpy(out_buf, fs->files[idx].content, FS_MAX_FILESIZE);
    return FS_OK;
}

/* ============================================================
 * fs_delete - Delete a file by name
 * Returns: FS_OK or error code
 * ============================================================ */
int fs_delete(HoneyFS *fs, const char *name) {
    int idx = fs_find(fs, name);
    if (idx < 0) return FS_ERR_NOT_FOUND;

    fs->files[idx].status = FS_SLOT_FREE;
    fs->files[idx].size   = 0;
    mem_set(fs->files[idx].name,    0, FS_MAX_FILENAME);
    mem_set(fs->files[idx].content, 0, FS_MAX_FILESIZE);
    fs->file_count--;

    return FS_OK;
}

/* ============================================================
 * fs_list - List all files (used by kernel for display)
 * NOTE: printing is done in main.c using the VGA functions
 * ============================================================ */
void fs_list(HoneyFS *fs) {
    /* Intentionally empty — listing is handled in main.c
     * so we can use the VGA print functions there directly */
}
