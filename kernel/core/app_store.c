#include <liquidos/app_store.h>
#include <liquidos/fs.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>

static const StoreApp catalog[] = {
    { "notes", "notes.lpkg", "STORE/PACKAGES/notes.lpkg", "Liquid Notes", "1.0", "Text notes app package", "APPS/NOTES.APP", "APPS/NOTES.APP", "APPS/HELLO.APP", "Productivity", "mint", (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD) | (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS) },
    { "paint", "paint.lpkg", "STORE/PACKAGES/paint.lpkg", "Liquid Paint", "1.0", "Simple drawing app package", "APPS/PAINT.APP", "APPS/PAINT.APP", "APPS/APP_A.APP", "Creative", "violet", (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD) | (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS) },
    { "calc", "calc.lpkg", "STORE/PACKAGES/calc.lpkg", "Liquid Calc", "1.0", "Calculator app package", "APPS/CALC.APP", "APPS/CALC.APP", "APPS/APP_B.APP", "Utility", "gold", (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD) | (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS) },
};

static char receipt_paths[sizeof(catalog) / sizeof(catalog[0])][FS_NAME_LENGTH];
static char manifest_paths[sizeof(catalog) / sizeof(catalog[0])][FS_NAME_LENGTH];

static LoadResult make_load_result(bool ok, u32 pid, const char *message) {
    LoadResult out;
    out.ok = ok;
    out.pid = pid;
    strncpy(out.message, message, sizeof(out.message) - 1);
    out.message[sizeof(out.message) - 1] = 0;
    return out;
}

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
}

static void append_dec(char *dest, size_t dest_size, u64 value) {
    char number[24];
    u64_to_dec(value, number, sizeof(number));
    append_text(dest, dest_size, number);
}

static void build_package_descriptor(const StoreApp *app, char *out, size_t out_size) {
    out[0] = 0;
    append_text(out, out_size, "LPKG1\n");
    append_text(out, out_size, "name=");
    append_text(out, out_size, app->name);
    append_text(out, out_size, "\ndisplay=");
    append_text(out, out_size, app->display_name);
    append_text(out, out_size, "\nversion=");
    append_text(out, out_size, app->version);
    append_text(out, out_size, "\nentry=");
    append_text(out, out_size, app->entry_path);
    append_text(out, out_size, "\npayload=");
    append_text(out, out_size, app->source_path);
    append_text(out, out_size, "\ncategory=");
    append_text(out, out_size, app->category);
    append_text(out, out_size, "\npermissions=");
    append_dec(out, out_size, app->syscall_mask);
    append_text(out, out_size, "\nfiles=1\n");
}

static StoreInstallCheck make_check(bool ok, size_t required_slots, const char *message) {
    StoreInstallCheck check;
    check.ok = ok;
    check.required_slots = required_slots;
    check.free_slots = fs_free_slots();
    strncpy(check.message, message, sizeof(check.message) - 1);
    check.message[sizeof(check.message) - 1] = 0;
    return check;
}

static bool package_has_field(const char *contents, const char *field, const char *value) {
    size_t field_length = strlen(field);
    size_t value_length = strlen(value);
    const char *cursor = contents;
    while (*cursor) {
        if (strncmp(cursor, field, field_length) == 0 &&
            strncmp(cursor + field_length, value, value_length) == 0) {
            char next = cursor[field_length + value_length];
            if (next == '\n' || next == 0) {
                return true;
            }
        }
        cursor++;
    }
    return false;
}

void app_store_init(void) {
    fs_write("STORE/CATALOG.TXT", "notes.lpkg\npaint.lpkg\ncalc.lpkg\nNetwork download transport pending TCP/IP.");
    for (size_t i = 0; i < app_store_count(); i++) {
        receipt_paths[i][0] = 0;
        append_text(receipt_paths[i], sizeof(receipt_paths[i]), "STORE/INSTALLED/");
        append_text(receipt_paths[i], sizeof(receipt_paths[i]), catalog[i].name);

        manifest_paths[i][0] = 0;
        append_text(manifest_paths[i], sizeof(manifest_paths[i]), "STORE/MANIFEST/");
        append_text(manifest_paths[i], sizeof(manifest_paths[i]), catalog[i].name);
        append_text(manifest_paths[i], sizeof(manifest_paths[i]), ".TXT");

        char package[FS_CONTENT_LENGTH];
        build_package_descriptor(&catalog[i], package, sizeof(package));
        fs_write(catalog[i].package_path, package);
    }
    serial_write_line("App store catalog initialized");
}

size_t app_store_count(void) {
    return sizeof(catalog) / sizeof(catalog[0]);
}

const StoreApp *app_store_get(size_t index) {
    if (index >= app_store_count()) {
        return NULL;
    }
    return &catalog[index];
}

StoreInstallCheck app_store_validate_by_index(size_t index) {
    const StoreApp *app = app_store_get(index);
    if (!app) {
        return make_check(false, 0, "app not found");
    }

    const FsFile *package = fs_find(app->package_path);
    if (!package || strncmp(package->contents, "LPKG1\n", 6) != 0) {
        return make_check(false, 0, "bad package descriptor");
    }
    if (!package_has_field(package->contents, "name=", app->name) ||
        !package_has_field(package->contents, "version=", app->version) ||
        !package_has_field(package->contents, "entry=", app->entry_path) ||
        !package_has_field(package->contents, "payload=", app->source_path)) {
        return make_check(false, 0, "package metadata mismatch");
    }

    const FsFile *source = fs_find(app->source_path);
    if (!source || source->size < sizeof(LappHeader)) {
        return make_check(false, 0, "payload missing");
    }
    const LappHeader *header = (const LappHeader *)(const void *)source->contents;
    if (memcmp(header->magic, LAPP_MAGIC, 4) != 0 || header->version != LAPP_VERSION) {
        return make_check(false, 0, "payload is not LAPP");
    }
    if ((header->syscall_mask & ~app->syscall_mask) != 0) {
        return make_check(false, 0, "payload permissions denied");
    }

    size_t required_slots = 0;
    if (!fs_find(app->installed_path)) {
        required_slots++;
    }
    if (!fs_find(app_store_receipt_path(index))) {
        required_slots++;
    }
    if (!fs_find(app_store_manifest_path(index))) {
        required_slots++;
    }
    if (fs_free_slots() < required_slots) {
        return make_check(false, required_slots, "not enough filesystem slots");
    }
    return make_check(true, required_slots, "package valid");
}

bool app_store_install_by_index(size_t index) {
    const StoreApp *app = app_store_get(index);
    if (!app) {
        return false;
    }

    StoreInstallCheck check = app_store_validate_by_index(index);
    if (!check.ok) {
        return false;
    }

    const FsFile *source = fs_find(app->source_path);
    if (!source || !fs_write_bytes(app->installed_path, (const u8 *)(const void *)source->contents, (size_t)source->size)) {
        return false;
    }

    char manifest[FS_CONTENT_LENGTH];
    manifest[0] = 0;
    append_text(manifest, sizeof(manifest), "format=LPKG1\n");
    append_text(manifest, sizeof(manifest), "name=");
    append_text(manifest, sizeof(manifest), app->display_name);
    append_text(manifest, sizeof(manifest), "\nversion=");
    append_text(manifest, sizeof(manifest), app->version);
    append_text(manifest, sizeof(manifest), "\npackage=");
    append_text(manifest, sizeof(manifest), app->package_name);
    append_text(manifest, sizeof(manifest), "\nentry=");
    append_text(manifest, sizeof(manifest), app->entry_path);
    append_text(manifest, sizeof(manifest), "\nsource=");
    append_text(manifest, sizeof(manifest), app->source_path);
    append_text(manifest, sizeof(manifest), "\ncategory=");
    append_text(manifest, sizeof(manifest), app->category);
    append_text(manifest, sizeof(manifest), "\naccent=");
    append_text(manifest, sizeof(manifest), app->accent);
    append_text(manifest, sizeof(manifest), "\ndescription=");
    append_text(manifest, sizeof(manifest), app->description);
    append_text(manifest, sizeof(manifest), "\npermissions=");
    append_dec(manifest, sizeof(manifest), app->syscall_mask);
    append_text(manifest, sizeof(manifest), "\n");

    return fs_write(app_store_receipt_path(index), app->installed_path) &&
           fs_write(app_store_manifest_path(index), manifest);
}

bool app_store_install(const char *name) {
    for (size_t i = 0; i < app_store_count(); i++) {
        if (strcmp(catalog[i].name, name) == 0 || strcmp(catalog[i].package_name, name) == 0) {
            return app_store_install_by_index(i);
        }
    }
    return false;
}

bool app_store_uninstall_by_index(size_t index) {
    const StoreApp *app = app_store_get(index);
    if (!app) {
        return false;
    }

    bool removed = false;
    if (fs_find(app->installed_path)) {
        removed = fs_delete(app->installed_path) || removed;
    }
    if (fs_find(app_store_receipt_path(index))) {
        removed = fs_delete(app_store_receipt_path(index)) || removed;
    }
    if (fs_find(app_store_manifest_path(index))) {
        removed = fs_delete(app_store_manifest_path(index)) || removed;
    }
    return removed;
}

bool app_store_uninstall(const char *name) {
    for (size_t i = 0; i < app_store_count(); i++) {
        if (strcmp(catalog[i].name, name) == 0 ||
            strcmp(catalog[i].package_name, name) == 0 ||
            strcmp(catalog[i].display_name, name) == 0) {
            return app_store_uninstall_by_index(i);
        }
    }
    return false;
}

LoadResult app_store_launch_by_index(size_t index) {
    const StoreApp *app = app_store_get(index);
    if (!app) {
        return make_load_result(false, 0, "app not found");
    }
    if (!app_store_is_installed(index)) {
        return make_load_result(false, 0, "app is not installed");
    }
    return loader_load_app(app->entry_path);
}

LoadResult app_store_launch(const char *name) {
    for (size_t i = 0; i < app_store_count(); i++) {
        if (strcmp(catalog[i].name, name) == 0 ||
            strcmp(catalog[i].package_name, name) == 0 ||
            strcmp(catalog[i].display_name, name) == 0) {
            return app_store_launch_by_index(i);
        }
    }
    return make_load_result(false, 0, "app not found");
}

bool app_store_is_installed(size_t index) {
    const StoreApp *app = app_store_get(index);
    return app && fs_find(app->installed_path) != NULL;
}

size_t app_store_installed_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < app_store_count(); i++) {
        if (app_store_is_installed(i)) {
            count++;
        }
    }
    return count;
}

const char *app_store_manifest_path(size_t index) {
    if (index >= app_store_count()) {
        return "";
    }
    return manifest_paths[index];
}

const char *app_store_receipt_path(size_t index) {
    if (index >= app_store_count()) {
        return "";
    }
    return receipt_paths[index];
}

const char *app_store_package_path(size_t index) {
    const StoreApp *app = app_store_get(index);
    return app ? app->package_path : "";
}
