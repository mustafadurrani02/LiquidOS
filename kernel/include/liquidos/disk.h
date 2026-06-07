#ifndef LIQUIDOS_DISK_H
#define LIQUIDOS_DISK_H

#include <liquidos/types.h>

void disk_init(void);
bool disk_is_available(void);
bool disk_read_sector(u32 lba, void *buffer);
bool disk_write_sector(u32 lba, const void *buffer);

#endif
