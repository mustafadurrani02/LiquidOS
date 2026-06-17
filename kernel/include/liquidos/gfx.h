#ifndef LIQUIDOS_GFX_H
#define LIQUIDOS_GFX_H

#include <liquidos/framebuffer.h>
#include <liquidos/types.h>

typedef u32 Color;

#define RGB(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | ((u32)(b)))

void gfx_init(Framebuffer *fb);
bool gfx_is_available(void);
u32 gfx_width(void);
u32 gfx_height(void);
void gfx_set_clip(i32 x, i32 y, i32 width, i32 height);
void gfx_clear_clip(void);
void gfx_clear(Color color);
void gfx_fill_rect(i32 x, i32 y, i32 width, i32 height, Color color);
void gfx_draw_rect(i32 x, i32 y, i32 width, i32 height, Color color);
void gfx_draw_line(i32 x0, i32 y0, i32 x1, i32 y1, Color color);
void gfx_fill_circle(i32 cx, i32 cy, i32 radius, Color color);
void gfx_fill_circle_alpha(i32 cx, i32 cy, i32 radius, Color color, u8 alpha);
void gfx_fill_round_rect(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color);
void gfx_fill_round_rect_plain_alpha(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color, u8 alpha);
void gfx_fill_round_rect_alpha(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color, u8 alpha);
void gfx_liquid_glass_rect(i32 x, i32 y, i32 width, i32 height, i32 radius);
void gfx_blur_round_rect(i32 x, i32 y, i32 width, i32 height, i32 radius);
void gfx_draw_round_rect_alpha(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color, u8 alpha);
void gfx_draw_round_rect(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color);
void gfx_draw_rgb565_image_scaled(i32 x, i32 y, i32 width, i32 height, const u16 *pixels, u32 src_width, u32 src_height);
void gfx_draw_argb8888_image_scaled(i32 x, i32 y, i32 width, i32 height, const u32 *pixels, u32 src_width, u32 src_height);
void gfx_prepare_wallpaper_rgb565(const u16 *pixels, u32 src_width, u32 src_height);
void gfx_draw_wallpaper(void);
void gfx_draw_char(i32 x, i32 y, char ch, Color color, u32 scale);
void gfx_draw_text(i32 x, i32 y, const char *text, Color color, u32 scale);
void gfx_draw_cursor(i32 x, i32 y);
void gfx_save_scene(void);
void gfx_restore_scene_rect(i32 x, i32 y, i32 width, i32 height);
void gfx_present(void);
void gfx_present_rect(i32 x, i32 y, i32 width, i32 height);
void gfx_present_cursor(i32 previous_x, i32 previous_y, i32 cursor_x, i32 cursor_y);

#endif
