#ifndef LIQUIDOS_FRAMEBUFFER_H
#define LIQUIDOS_FRAMEBUFFER_H

#include <liquidos/boot.h>
#include <liquidos/types.h>

#define FRAMEBUFFER_MAX_WIDTH 1920
#define FRAMEBUFFER_MAX_HEIGHT 1080

typedef struct Framebuffer {
    u8 *address;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    bool available;
} Framebuffer;

void framebuffer_init(const BootInfo *boot);
Framebuffer *framebuffer_get(void);

#endif
