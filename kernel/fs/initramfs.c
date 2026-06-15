#include <liquidos/fs.h>
#include <liquidos/disk.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>

#define FS_MAX_FILES 24
#define FS_DISK_MAGIC 0x53464C51U
#define FS_DISK_VERSION 1U
#define FS_DISK_LBA 16U
#define FS_DISK_SECTORS 32U

static FsFile files[FS_MAX_FILES];
static u8 disk_image[FS_DISK_SECTORS * 512];

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
    files[slot].used = true;
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
}

void fs_init(void) {
    fs_load_defaults();
    if (!fs_load_from_disk()) {
        fs_save_to_disk();
    }
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
