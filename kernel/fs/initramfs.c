#include <liquidos/fs.h>
#include <liquidos/disk.h>
#include <liquidos/loader.h>
#include <liquidos/lib.h>
#include <liquidos/scheduler.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>
#include <liquidos/vmm.h>

#define FS_MAX_FILES 40
#define FS_DISK_MAGIC 0x53464C51U
#define FS_DISK_VERSION 1U
#define FS_DISK_LBA 16U
#define FS_DISK_SECTORS 48U

static FsFile files[FS_MAX_FILES];
static u8 disk_image[FS_DISK_SECTORS * 512];

static u64 fs_modified_now(void) {
    u64 tick = scheduler_ticks();
    return tick ? tick : 1;
}

typedef struct FsDiskHeader {
    u32 magic;
    u32 version;
    u32 bytes;
    u32 checksum;
} FsDiskHeader;

static bool valid_name(const char *name) {
    size_t length = strlen(name);
    if (length == 0 || length >= FS_NAME_LENGTH) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        char ch = name[i];
        bool ok = (ch >= 'A' && ch <= 'Z') ||
                  (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') ||
                  ch == '/' || ch == '.' || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static void set_file(size_t slot, const char *name, const char *contents) {
    strncpy(files[slot].name, name, FS_NAME_LENGTH - 1);
    files[slot].name[FS_NAME_LENGTH - 1] = 0;
    strncpy(files[slot].contents, contents, FS_CONTENT_LENGTH - 1);
    files[slot].contents[FS_CONTENT_LENGTH - 1] = 0;
    files[slot].size = strlen(files[slot].contents);
    files[slot].modified_tick = fs_modified_now();
    files[slot].used = true;
}

static void set_file_bytes(size_t slot, const char *name, const u8 *contents, size_t size) {
    if (size > FS_CONTENT_LENGTH) {
        size = FS_CONTENT_LENGTH;
    }
    strncpy(files[slot].name, name, FS_NAME_LENGTH - 1);
    files[slot].name[FS_NAME_LENGTH - 1] = 0;
    memset(files[slot].contents, 0, sizeof(files[slot].contents));
    memcpy(files[slot].contents, contents, size);
    files[slot].size = size;
    files[slot].modified_tick = fs_modified_now();
    files[slot].used = true;
}

static void write_u32(u8 *out, u32 value) {
    out[0] = (u8)(value & 0xFF);
    out[1] = (u8)((value >> 8) & 0xFF);
    out[2] = (u8)((value >> 16) & 0xFF);
    out[3] = (u8)((value >> 24) & 0xFF);
}

static void emit_mov_rax(u8 *out, size_t *index, u32 value) {
    out[(*index)++] = 0x48;
    out[(*index)++] = 0xC7;
    out[(*index)++] = 0xC0;
    write_u32(&out[*index], value);
    *index += 4;
}

static void emit_write_message(u8 *out, size_t *index, u32 message_offset) {
    emit_mov_rax(out, index, SYS_WRITE);
    out[(*index)++] = 0x48;
    out[(*index)++] = 0x8D;
    out[(*index)++] = 0x1D;
    write_u32(&out[*index], message_offset - (u32)(*index + 4));
    *index += 4;
    out[(*index)++] = 0xCD;
    out[(*index)++] = 0x80;
}

static void emit_yield(u8 *out, size_t *index) {
    emit_mov_rax(out, index, SYS_YIELD);
    out[(*index)++] = 0xCD;
    out[(*index)++] = 0x80;
}

static void emit_exit(u8 *out, size_t *index) {
    emit_mov_rax(out, index, SYS_EXIT);
    out[(*index)++] = 0x48;
    out[(*index)++] = 0x31;
    out[(*index)++] = 0xDB;
    out[(*index)++] = 0xCD;
    out[(*index)++] = 0x80;
    out[(*index)++] = 0xEB;
    out[(*index)++] = 0xFE;
}

static size_t build_lapp(u8 *out, const char *message, u32 repeats, bool yield_between) {
    memset(out, 0, FS_CONTENT_LENGTH);

    LappHeader *header = (LappHeader *)(void *)out;
    memcpy(header->magic, LAPP_MAGIC, 4);
    header->version = LAPP_VERSION;
    header->header_size = sizeof(LappHeader);
    header->entry_offset = 0;
    header->text_offset = sizeof(LappHeader);
    header->stack_size = VMM_PAGE_SIZE;
    header->syscall_mask = (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD) |
                           (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS);

    u8 text[256];
    memset(text, 0, sizeof(text));
    size_t index = 0;
    u32 body_size = repeats * (yield_between ? 25U : 16U) + 12U;
    u32 message_offset = body_size;

    for (u32 i = 0; i < repeats; i++) {
        emit_write_message(text, &index, message_offset);
        if (yield_between) {
            emit_yield(text, &index);
        }
    }
    emit_exit(text, &index);
    memcpy(&text[message_offset], message, strlen(message) + 1);

    header->text_size = message_offset + strlen(message) + 1;
    memcpy(out + header->text_offset, text, (size_t)header->text_size);
    return (size_t)(header->text_offset + header->text_size);
}

static size_t build_crash_lapp(u8 *out) {
    memset(out, 0, FS_CONTENT_LENGTH);

    LappHeader *header = (LappHeader *)(void *)out;
    memcpy(header->magic, LAPP_MAGIC, 4);
    header->version = LAPP_VERSION;
    header->header_size = sizeof(LappHeader);
    header->entry_offset = 0;
    header->text_offset = sizeof(LappHeader);
    header->stack_size = VMM_PAGE_SIZE;
    header->syscall_mask = (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD);

    u8 *text = out + header->text_offset;
    size_t index = 0;
    text[index++] = 0x48; text[index++] = 0x31; text[index++] = 0xC0; /* xor rax, rax */
    text[index++] = 0x48; text[index++] = 0x8B; text[index++] = 0x00; /* mov rax, [rax] */
    text[index++] = 0xEB; text[index++] = 0xFE;                       /* should never run */
    header->text_size = index;
    return (size_t)(header->text_offset + header->text_size);
}

static u32 checksum_bytes(const u8 *data, size_t count) {
    u32 sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum = (sum << 5) ^ (sum >> 27) ^ data[i];
    }
    return sum;
}

static void fs_save_to_disk(void) {
    if (!disk_is_available()) {
        return;
    }

    memset(disk_image, 0, sizeof(disk_image));
    FsDiskHeader *header = (FsDiskHeader *)disk_image;
    header->magic = FS_DISK_MAGIC;
    header->version = FS_DISK_VERSION;
    header->bytes = sizeof(files);
    memcpy(disk_image + sizeof(FsDiskHeader), files, sizeof(files));
    header->checksum = checksum_bytes(disk_image + sizeof(FsDiskHeader), sizeof(files));

    for (u32 i = 0; i < FS_DISK_SECTORS; i++) {
        if (!disk_write_sector(FS_DISK_LBA + i, disk_image + i * 512)) {
            serial_write_line("LiquidFS save failed");
            return;
        }
    }
}

static bool fs_load_from_disk(void) {
    if (!disk_is_available()) {
        return false;
    }

    for (u32 i = 0; i < FS_DISK_SECTORS; i++) {
        if (!disk_read_sector(FS_DISK_LBA + i, disk_image + i * 512)) {
            return false;
        }
    }

    FsDiskHeader *header = (FsDiskHeader *)disk_image;
    if (header->magic != FS_DISK_MAGIC || header->version != FS_DISK_VERSION || header->bytes != sizeof(files)) {
        return false;
    }
    if (header->checksum != checksum_bytes(disk_image + sizeof(FsDiskHeader), sizeof(files))) {
        serial_write_line("LiquidFS checksum mismatch");
        return false;
    }

    memcpy(files, disk_image + sizeof(FsDiskHeader), sizeof(files));
    serial_write_line("LiquidFS loaded from disk");
    return true;
}

static void fs_load_defaults(void) {
    memset(files, 0, sizeof(files));

    set_file(0, "README.TXT", "Welcome to LiquidOS. This RAM filesystem can create, edit, read, and delete files while the OS is running.");
    set_file(1, "SYSTEM/BOOT.TXT", "Boot path: BIOS -> Stage 1 -> Stage 2 -> x86_64 kernel -> LiquidOS desktop.");
    set_file(2, "DESKTOP/NOTES.TXT", "The desktop, terminal, browser, and file manager are all custom kernel components.");
    set_file(3, "APPS/TERMINAL.APP", "Built-in terminal application.");
    set_file(4, "APPS/LIQUEIA.APP", "Native Liqueia browser shell for LiquidOS.");
    set_file(5, "APPS/FILES.APP", "Built-in graphical file manager.");
    set_file(6, "WEB/HOME.HTML", "Liqueia native start page.");
    set_file(7, "WEB/DOCS.HTML", "Liqueia is ready for the future LiquidOS network stack.");
    set_file(8, "WEB/ABOUT.HTML", "Native port based on mustafadurrani02/Liqueia.");
    set_file(9, "HOME/.DIR", "LiquidOS directory marker.");
    set_file(10, "DOWNLOADS/.DIR", "LiquidOS directory marker.");
    set_file(11, "DOCUMENTS/PROJECTS/.DIR", "LiquidOS directory marker.");
    set_file(12, "PICTURES/.DIR", "LiquidOS directory marker.");
    set_file(13, "MUSIC/.DIR", "LiquidOS directory marker.");
    set_file(14, "VIDEOS/.DIR", "LiquidOS directory marker.");
    set_file(15, "TRASH/.DIR", "LiquidOS directory marker.");
    set_file(16, "DOCUMENTS/README.TXT", "Documents is now browsable from the LiquidOS File Explorer.");
    set_file(17, "DOWNLOADS/WELCOME.TXT", "Downloaded files appear here.");
    set_file(18, "PICTURES/WALLPAPER.TXT", "Wallpaper assets and image notes live here.");

    u8 app[FS_CONTENT_LENGTH];
    size_t size = build_lapp(app, "hello.app from flat LAPP format\n", 1, false);
    set_file_bytes(19, "APPS/HELLO.APP", app, size);
    size = build_lapp(app, "app A yielded from ring 3\n", 3, true);
    set_file_bytes(20, "APPS/APP_A.APP", app, size);
    size = build_lapp(app, "app B yielded from ring 3\n", 3, true);
    set_file_bytes(21, "APPS/APP_B.APP", app, size);
    size = build_crash_lapp(app);
    set_file_bytes(22, "APPS/CRASH.APP", app, size);
}

void fs_init(void) {
    fs_load_defaults();
    if (!fs_load_from_disk()) {
        fs_save_to_disk();
    }
}

size_t fs_capacity(void) {
    return FS_MAX_FILES;
}

size_t fs_file_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) {
            count++;
        }
    }
    return count;
}

size_t fs_free_slots(void) {
    return FS_MAX_FILES - fs_file_count();
}

bool fs_persistence_available(void) {
    return disk_is_available();
}

const FsFile *fs_get_file(size_t index) {
    size_t current = 0;
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            continue;
        }
        if (current == index) {
            return &files[i];
        }
        current++;
    }
    return NULL;
}

i32 fs_find_index(const char *name) {
    size_t current = 0;
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            continue;
        }
        if (strcmp(files[i].name, name) == 0) {
            return (i32)current;
        }
        current++;
    }
    return -1;
}

const FsFile *fs_find(const char *name) {
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) {
            return &files[i];
        }
    }
    return NULL;
}

bool fs_create(const char *name) {
    if (!valid_name(name) || fs_find(name)) {
        return false;
    }

    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            set_file(i, name, "");
            fs_save_to_disk();
            return true;
        }
    }
    return false;
}

bool fs_write(const char *name, const char *contents) {
    if (!valid_name(name)) {
        return false;
    }

    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) {
            strncpy(files[i].contents, contents, FS_CONTENT_LENGTH - 1);
            files[i].contents[FS_CONTENT_LENGTH - 1] = 0;
            files[i].size = strlen(files[i].contents);
            files[i].modified_tick = fs_modified_now();
            fs_save_to_disk();
            return true;
        }
    }

    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            set_file(i, name, contents);
            fs_save_to_disk();
            return true;
        }
    }
    return false;
}

bool fs_write_bytes(const char *name, const u8 *contents, size_t size) {
    if (!valid_name(name) || !contents) {
        return false;
    }
    if (size > FS_CONTENT_LENGTH) {
        size = FS_CONTENT_LENGTH;
    }

    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) {
            memset(files[i].contents, 0, sizeof(files[i].contents));
            memcpy(files[i].contents, contents, size);
            files[i].size = size;
            files[i].modified_tick = fs_modified_now();
            fs_save_to_disk();
            return true;
        }
    }

    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            set_file_bytes(i, name, contents, size);
            fs_save_to_disk();
            return true;
        }
    }
    return false;
}

bool fs_copy(const char *source, const char *dest) {
    if (!valid_name(source) || !valid_name(dest) || fs_find(dest)) {
        return false;
    }

    const FsFile *file = fs_find(source);
    if (!file) {
        return false;
    }

    return fs_write_bytes(dest, (const u8 *)(const void *)file->contents, (size_t)file->size);
}

bool fs_rename(const char *old_name, const char *new_name) {
    if (!valid_name(old_name) || !valid_name(new_name) || fs_find(new_name)) {
        return false;
    }

    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, old_name) == 0) {
            strncpy(files[i].name, new_name, FS_NAME_LENGTH - 1);
            files[i].name[FS_NAME_LENGTH - 1] = 0;
            files[i].modified_tick = fs_modified_now();
            fs_save_to_disk();
            return true;
        }
    }
    return false;
}

bool fs_delete(const char *name) {
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) {
            memset(&files[i], 0, sizeof(files[i]));
            fs_save_to_disk();
            return true;
        }
    }
    return false;
}
