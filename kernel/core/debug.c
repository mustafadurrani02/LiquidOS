#include <liquidos/debug.h>
#include <liquidos/gfx.h>
#include <liquidos/io.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>

static void draw_panic_screen(const char *message, const char *file, u32 line) {
    if (!gfx_is_available()) {
        return;
    }

    gfx_clear(RGB(18, 24, 32));
    gfx_fill_rect(0, 0, (i32)gfx_width(), 72, RGB(118, 35, 45));
    gfx_draw_text(24, 24, "LiquidOS stopped", RGB(255, 245, 240), 1);
    gfx_draw_text(24, 104, message ? message : "Kernel panic", RGB(255, 255, 255), 1);

    if (file) {
        char line_text[32];
        u64_to_dec(line, line_text, sizeof(line_text));
        gfx_draw_text(24, 136, file, RGB(214, 224, 230), 1);
        gfx_draw_text(24, 164, line_text, RGB(214, 224, 230), 1);
    }

    gfx_draw_text(24, 220, "Check build/serial.log for details.", RGB(184, 198, 205), 1);
    gfx_present();
}

void panic_at(const char *message, const char *file, u32 line) {
    interrupts_disable();
    serial_write_line("");
    serial_write_line("=== LiquidOS panic ===");
    serial_write_line(message ? message : "Kernel panic");
    if (file) {
        serial_write(file);
        serial_write(":");
        char line_text[32];
        u64_to_dec(line, line_text, sizeof(line_text));
        serial_write_line(line_text);
    }

    draw_panic_screen(message, file, line);

    for (;;) {
        cpu_pause();
    }
}

void panic(const char *message) {
    panic_at(message, NULL, 0);
}
