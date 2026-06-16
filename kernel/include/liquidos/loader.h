#ifndef LIQUIDOS_LOADER_H
#define LIQUIDOS_LOADER_H

#include <liquidos/types.h>

typedef struct LoadResult {
    bool ok;
    u32 pid;
    char message[64];
} LoadResult;

void loader_init(void);
LoadResult loader_load_app(const char *path);

#endif
