#include <liquidos/app_store.h>
#include <liquidos/fs.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>

static const StoreApp catalog[] = {
    { "notes", "notes.lpkg", "Liquid Notes", "1.0", "Text notes app package", "APPS/NOTES.APP", "APPS/NOTES.APP", "Productivity", "mint" },
    { "paint", "paint.lpkg", "Liquid Paint", "1.0", "Simple drawing app package", "APPS/PAINT.APP", "APPS/PAINT.APP", "Creative", "violet" },
    { "calc", "calc.lpkg", "Liquid Calc", "1.0", "Calculator app package", "APPS/CALC.APP", "APPS/CALC.APP", "Utility", "gold" },
};

static char receipt_paths[sizeof(catalog) / sizeof(catalog[0])][FS_NAME_LENGTH];
static char manifest_paths[sizeof(catalog) / sizeof(catalog[0])][FS_NAME_LENGTH];

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
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

bool app_store_install_by_index(size_t index) {
    const StoreApp *app = app_store_get(index);
    if (!app) {
        return false;
    }

    char contents[FS_CONTENT_LENGTH];
    contents[0] = 0;
    append_text(contents, sizeof(contents), "Installed package: ");
    append_text(contents, sizeof(contents), app->package_name);
    append_text(contents, sizeof(contents), "\n");
    append_text(contents, sizeof(contents), app->display_name);
    append_text(contents, sizeof(contents), "\n");
    append_text(contents, sizeof(contents), app->description);
    append_text(contents, sizeof(contents), "\nCategory: ");
    append_text(contents, sizeof(contents), app->category);
    append_text(contents, sizeof(contents), "\nStatus: installed from the LiquidOS Store.");

    if (!fs_write(app->installed_path, contents)) {
        return false;
    }

    char manifest[FS_CONTENT_LENGTH];
    manifest[0] = 0;
    append_text(manifest, sizeof(manifest), "name=");
    append_text(manifest, sizeof(manifest), app->display_name);
    append_text(manifest, sizeof(manifest), "\nversion=");
    append_text(manifest, sizeof(manifest), app->version);
    append_text(manifest, sizeof(manifest), "\npackage=");
    append_text(manifest, sizeof(manifest), app->package_name);
    append_text(manifest, sizeof(manifest), "\nentry=");
    append_text(manifest, sizeof(manifest), app->entry_path);
    append_text(manifest, sizeof(manifest), "\ncategory=");
    append_text(manifest, sizeof(manifest), app->category);
    append_text(manifest, sizeof(manifest), "\naccent=");
    append_text(manifest, sizeof(manifest), app->accent);
    append_text(manifest, sizeof(manifest), "\ndescription=");
    append_text(manifest, sizeof(manifest), app->description);
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
