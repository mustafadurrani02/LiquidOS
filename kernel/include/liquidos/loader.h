#ifndef LIQUIDOS_LOADER_H
#define LIQUIDOS_LOADER_H

#include <liquidos/types.h>

typedef struct LoadResult {
    bool ok;
    u32 pid;
    char message[64];
} LoadResult;

#define LAPP_MAGIC "LAPP"
#define LAPP_VERSION 1U

typedef struct LappHeader {
    char magic[4];
    u32 version;
    u32 header_size;
    u64 entry_offset;
    u64 text_offset;
    u64 text_size;
    u64 data_offset;
    u64 data_size;
    u64 bss_size;
    u64 stack_size;
    u64 syscall_mask;
} __attribute__((packed)) LappHeader;

void loader_init(void);
LoadResult loader_spawn_app(const char *path);
LoadResult loader_load_app(const char *path);
LoadResult loader_run_apps(const char *left_path, const char *right_path);

#endif
