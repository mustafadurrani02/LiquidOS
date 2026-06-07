#ifndef LIQUIDOS_APP_H
#define LIQUIDOS_APP_H

#include <liquidos/types.h>

typedef enum AppId {
    APP_TERMINAL = 1,
    APP_BROWSER = 2,
    APP_FILES = 3
} AppId;

typedef struct AppDescriptor {
    AppId id;
    const char *name;
    const char *path;
    bool built_in;
} AppDescriptor;

void app_init(void);
size_t app_count(void);
const AppDescriptor *app_get(size_t index);
const AppDescriptor *app_find_by_path(const char *path);

#endif
