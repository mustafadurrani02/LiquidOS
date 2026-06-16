#ifndef LIQUIDOS_APP_STORE_H
#define LIQUIDOS_APP_STORE_H

#include <liquidos/types.h>

typedef struct StoreApp {
    const char *name;
    const char *package_name;
    const char *description;
    const char *installed_path;
} StoreApp;

void app_store_init(void);
size_t app_store_count(void);
const StoreApp *app_store_get(size_t index);
bool app_store_install_by_index(size_t index);
bool app_store_install(const char *name);

#endif
