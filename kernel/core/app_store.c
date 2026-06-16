#include <liquidos/app_store.h>
#include <liquidos/fs.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>

static const StoreApp catalog[] = {
    { "notes", "notes.lpkg", "Text notes app package", "APPS/NOTES.APP" },
    { "paint", "paint.lpkg", "Simple drawing app package", "APPS/PAINT.APP" },
    { "calc", "calc.lpkg", "Calculator app package", "APPS/CALC.APP" },
};

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
}

void app_store_init(void) {
    fs_write("STORE/CATALOG.TXT", "notes.lpkg\npaint.lpkg\ncalc.lpkg\nNetwork download transport pending TCP/IP.");
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
    append_text(contents, sizeof(contents), app->description);
    append_text(contents, sizeof(contents), "\nLaunch integration pending user-app ABI.");

    return fs_write(app->installed_path, contents);
}

bool app_store_install(const char *name) {
    for (size_t i = 0; i < app_store_count(); i++) {
        if (strcmp(catalog[i].name, name) == 0 || strcmp(catalog[i].package_name, name) == 0) {
            return app_store_install_by_index(i);
        }
    }
    return false;
}
