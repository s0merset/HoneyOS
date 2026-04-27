#ifndef HONEYFS_H
#define HONEYFS_H

#define FS_MAX_FILES        16
#define FS_MAX_FILENAME     32
#define FS_MAX_FILESIZE     512
#define FS_MAX_CONTENT      FS_MAX_FILESIZE

#define FS_SLOT_FREE        0
#define FS_SLOT_USED        1

#define FS_OK               0
#define FS_ERR_NOT_FOUND   -1
#define FS_ERR_EXISTS      -2
#define FS_ERR_FULL        -3
#define FS_ERR_NAME        -4
#define FS_ERR_SIZE        -5

typedef struct {
    int    status;
    char   name[FS_MAX_FILENAME];
    char   content[FS_MAX_FILESIZE];
    int    size;
} FileEntry;

typedef struct {
    FileEntry files[FS_MAX_FILES];
    int       file_count;
} HoneyFS;

void fs_init(HoneyFS *fs);
int  fs_create(HoneyFS *fs, const char *name);
int  fs_write(HoneyFS *fs, const char *name, const char *content);
int  fs_read(HoneyFS *fs, const char *name, char *out_buf);
int  fs_delete(HoneyFS *fs, const char *name);
void fs_list(HoneyFS *fs);
int  fs_find(HoneyFS *fs, const char *name);

#endif
