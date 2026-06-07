#include <liquidos/app.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>

static const AppDescriptor apps[] = {
    { APP_TERMINAL, "Terminal", "APPS/TERMINAL.APP", true },
    { APP_BROWSER, "Browser", "APPS/BROWSER.APP", true },
    { APP_FILES, "Files", "APPS/FILES.APP", true },
};

void app_init(void) {
    serial_write_line("Built-in app registry initialized");
}

size_t app_count(void) {
    return sizeof(apps) / sizeof(apps[0]);
}

const AppDescriptor *app_get(size_t index) {
    if (index >= app_count()) {
        return NULL;
    }
    return &apps[index];
}

const AppDescriptor *app_find_by_path(const char *path) {
    for (size_t i = 0; i < app_count(); i++) {
        if (strcmp(apps[i].path, path) == 0) {
            return &apps[i];
        }
    }
    return NULL;
}
