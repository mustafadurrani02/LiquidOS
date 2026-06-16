#ifndef LIQUIDOS_APP_STORE_H
#define LIQUIDOS_APP_STORE_H

#include <liquidos/types.h>

typedef struct StoreApp {
    const char *name;
    const char *package_name;
    const char *display_name;
    const char *version;
    const char *description;
    const char *installed_path;
    const char *entry_path;
    const char *category;
    const char *accent;
} StoreApp;

void app_store_init(void);
size_t app_store_count(void);
const StoreApp *app_store_get(size_t index);
bool app_store_install_by_index(size_t index);
bool app_store_install(const char *name);
bool app_store_uninstall_by_index(size_t index);
bool app_store_uninstall(const char *name);
bool app_store_is_installed(size_t index);
size_t app_store_installed_count(void);
const char *app_store_manifest_path(size_t index);
const char *app_store_receipt_path(size_t index);

#endif
