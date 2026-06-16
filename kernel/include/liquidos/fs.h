#ifndef LIQUIDOS_FS_H
#define LIQUIDOS_FS_H

#include <liquidos/types.h>

#define FS_NAME_LENGTH 40
#define FS_CONTENT_LENGTH 384

typedef struct FsFile {
    char name[FS_NAME_LENGTH];
    char contents[FS_CONTENT_LENGTH];
    u64 size;
    bool used;
} FsFile;

void fs_init(void);
size_t fs_file_count(void);
const FsFile *fs_get_file(size_t index);
const FsFile *fs_find(const char *name);
i32 fs_find_index(const char *name);
bool fs_create(const char *name);
bool fs_write(const char *name, const char *contents);
bool fs_write_bytes(const char *name, const u8 *contents, size_t size);
bool fs_delete(const char *name);

#endif
