#include <liquidos/framebuffer.h>

static Framebuffer framebuffer;

void framebuffer_init(const BootInfo *boot) {
    framebuffer.address = (u8 *)(uintptr_t)boot->framebuffer_address;
    framebuffer.width = boot->framebuffer_width;
    framebuffer.height = boot->framebuffer_height;
    framebuffer.pitch = boot->framebuffer_pitch;
    framebuffer.bpp = boot->framebuffer_bpp;

    framebuffer.available =
        framebuffer.address != NULL &&
        framebuffer.width > 0 &&
        framebuffer.height > 0 &&
        framebuffer.width <= FRAMEBUFFER_MAX_WIDTH &&
        framebuffer.height <= FRAMEBUFFER_MAX_HEIGHT &&
        (framebuffer.bpp == 24 || framebuffer.bpp == 32);
}

Framebuffer *framebuffer_get(void) {
    return &framebuffer;
}
