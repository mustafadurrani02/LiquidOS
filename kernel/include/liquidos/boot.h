#ifndef LIQUIDOS_BOOT_H
#define LIQUIDOS_BOOT_H

#include <liquidos/types.h>

#define BOOT_INFO_MAGIC 0x4451494C
#define BOOT_MEMORY_USABLE 1

typedef struct BootMemoryMapEntry {
    u64 base;
    u64 length;
    u32 type;
    u32 acpi;
} BootMemoryMapEntry;

typedef struct BootInfo {
    u32 magic;
    u32 version;
    u32 boot_drive;
    u32 memory_map_count;
    u64 framebuffer_address;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u32 framebuffer_pitch;
    u32 framebuffer_bpp;
    u64 kernel_start;
    u64 kernel_size;
    BootMemoryMapEntry memory_map[32];
} BootInfo;

#endif
