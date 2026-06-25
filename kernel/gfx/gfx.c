#include <liquidos/gfx.h>
#include <liquidos/lib.h>
#include "cursor_image.h"
#include "ui_font.h"

void font_get_rows(char ch, u8 rows[7]);

static Framebuffer *active_fb;
static u32 back_buffer[FRAMEBUFFER_MAX_WIDTH * FRAMEBUFFER_MAX_HEIGHT];
static u32 scene_buffer[FRAMEBUFFER_MAX_WIDTH * FRAMEBUFFER_MAX_HEIGHT];
static u32 wallpaper_buffer[FRAMEBUFFER_MAX_WIDTH * FRAMEBUFFER_MAX_HEIGHT];
static bool gfx_ready = false;
static bool wallpaper_ready = false;
static bool clip_enabled = false;
static i32 clip_x0 = 0;
static i32 clip_y0 = 0;
static i32 clip_x1 = 0;
static i32 clip_y1 = 0;

static void put_pixel(i32 x, i32 y, Color color) {
    if (!gfx_ready || x < 0 || y < 0) {
        return;
    }
    if ((u32)x >= active_fb->width || (u32)y >= active_fb->height) {
        return;
    }
    if (clip_enabled && (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1)) {
        return;
    }
    back_buffer[(u32)y * active_fb->width + (u32)x] = color;
}

static Color get_pixel(i32 x, i32 y) {
    if (!gfx_ready || x < 0 || y < 0) {
        return 0;
    }
    if ((u32)x >= active_fb->width || (u32)y >= active_fb->height) {
        return 0;
    }
    return back_buffer[(u32)y * active_fb->width + (u32)x];
}

static Color blend(Color bottom, Color top, u8 alpha) {
    u32 inv = 255 - alpha;
    u32 br = (bottom >> 16) & 0xFF;
    u32 bg = (bottom >> 8) & 0xFF;
    u32 bb = bottom & 0xFF;
    u32 tr = (top >> 16) & 0xFF;
    u32 tg = (top >> 8) & 0xFF;
    u32 tb = top & 0xFF;
    return RGB((br * inv + tr * alpha) / 255, (bg * inv + tg * alpha) / 255, (bb * inv + tb * alpha) / 255);
}

static Color unpack_rgb565(u16 packed) {
    u8 r = (u8)((((packed >> 11) & 0x1F) * 255) / 31);
    u8 g = (u8)((((packed >> 5) & 0x3F) * 255) / 63);
    u8 b = (u8)(((packed & 0x1F) * 255) / 31);
    return RGB(r, g, b);
}

static u8 lerp_u8(u8 a, u8 b, u32 amount) {
    return (u8)(((u32)a * (65536 - amount) + (u32)b * amount) >> 16);
}

static Color lerp_color(Color a, Color b, u32 amount) {
    return RGB(
        lerp_u8((u8)((a >> 16) & 0xFF), (u8)((b >> 16) & 0xFF), amount),
        lerp_u8((u8)((a >> 8) & 0xFF), (u8)((b >> 8) & 0xFF), amount),
        lerp_u8((u8)(a & 0xFF), (u8)(b & 0xFF), amount)
    );
}

static bool inside_round_rect(i32 px, i32 py, i32 x, i32 y, i32 width, i32 height, i32 radius) {
    if (px < x || py < y || px >= x + width || py >= y + height) {
        return false;
    }
    if (radius <= 0) {
        return true;
    }

    i32 left = x + radius;
    i32 right = x + width - radius - 1;
    i32 top = y + radius;
    i32 bottom = y + height - radius - 1;

    i32 cx = px < left ? left : (px > right ? right : px);
    i32 cy = py < top ? top : (py > bottom ? bottom : py);
    i32 dx = px - cx;
    i32 dy = py - cy;
    return dx * dx + dy * dy <= radius * radius;
}

static u8 round_rect_coverage(i32 px, i32 py, i32 x, i32 y, i32 width, i32 height, i32 radius) {
    if (px < x || py < y || px >= x + width || py >= y + height) {
        return 0;
    }
    if (radius <= 0) {
        return 255;
    }
    if (radius * 2 > width) {
        radius = width / 2;
    }
    if (radius * 2 > height) {
        radius = height / 2;
    }

    i32 left = x + radius;
    i32 right = x + width - radius - 1;
    i32 top = y + radius;
    i32 bottom = y + height - radius - 1;
    i32 cx = px < left ? left : (px > right ? right : px);
    i32 cy = py < top ? top : (py > bottom ? bottom : py);
    i32 dx = px - cx;
    i32 dy = py - cy;
    i32 dist2 = dx * dx + dy * dy;
    i32 inner = (radius - 1) * (radius - 1);
    i32 outer = radius * radius;

    if (dist2 <= inner) {
        return 255;
    }
    if (dist2 > outer) {
        return 0;
    }

    i32 span = outer - inner;
    if (span <= 0) {
        return 255;
    }
    return (u8)(((outer - dist2) * 255) / span);
}

void gfx_init(Framebuffer *fb) {
    active_fb = fb;
    gfx_ready = fb && fb->available;
    clip_enabled = false;
}

bool gfx_is_available(void) {
    return gfx_ready;
}

u32 gfx_width(void) {
    return gfx_ready ? active_fb->width : 80;
}

u32 gfx_height(void) {
    return gfx_ready ? active_fb->height : 25;
}

void gfx_set_clip(i32 x, i32 y, i32 width, i32 height) {
    if (!gfx_ready || width <= 0 || height <= 0) {
        clip_enabled = false;
        return;
    }

    clip_x0 = x < 0 ? 0 : x;
    clip_y0 = y < 0 ? 0 : y;
    clip_x1 = x + width;
    clip_y1 = y + height;
    if (clip_x1 > (i32)active_fb->width) {
        clip_x1 = (i32)active_fb->width;
    }
    if (clip_y1 > (i32)active_fb->height) {
        clip_y1 = (i32)active_fb->height;
    }
    clip_enabled = clip_x0 < clip_x1 && clip_y0 < clip_y1;
}

void gfx_clear_clip(void) {
    clip_enabled = false;
}

void gfx_clear(Color color) {
    if (!gfx_ready) {
        return;
    }

    u32 pixels = active_fb->width * active_fb->height;
    for (u32 i = 0; i < pixels; i++) {
        back_buffer[i] = color;
    }
}

void gfx_fill_rect(i32 x, i32 y, i32 width, i32 height, Color color) {
    if (!gfx_ready || width <= 0 || height <= 0) {
        return;
    }

    i32 x0 = x < 0 ? 0 : x;
    i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + width;
    i32 y1 = y + height;

    if (x1 > (i32)active_fb->width) {
        x1 = (i32)active_fb->width;
    }
    if (y1 > (i32)active_fb->height) {
        y1 = (i32)active_fb->height;
    }
    if (clip_enabled) {
        if (x0 < clip_x0) {
            x0 = clip_x0;
        }
        if (y0 < clip_y0) {
            y0 = clip_y0;
        }
        if (x1 > clip_x1) {
            x1 = clip_x1;
        }
        if (y1 > clip_y1) {
            y1 = clip_y1;
        }
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (i32 py = y0; py < y1; py++) {
        u32 *row = &back_buffer[(u32)py * active_fb->width];
        for (i32 px = x0; px < x1; px++) {
            row[px] = color;
        }
    }
}

void gfx_draw_rect(i32 x, i32 y, i32 width, i32 height, Color color) {
    gfx_fill_rect(x, y, width, 1, color);
    gfx_fill_rect(x, y + height - 1, width, 1, color);
    gfx_fill_rect(x, y, 1, height, color);
    gfx_fill_rect(x + width - 1, y, 1, height, color);
}

static i32 abs_i32(i32 value) {
    return value < 0 ? -value : value;
}

void gfx_draw_line(i32 x0, i32 y0, i32 x1, i32 y1, Color color) {
    i32 dx = abs_i32(x1 - x0);
    i32 sx = x0 < x1 ? 1 : -1;
    i32 dy = -abs_i32(y1 - y0);
    i32 sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy;

    for (;;) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        i32 e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_fill_circle(i32 cx, i32 cy, i32 radius, Color color) {
    if (radius <= 0) {
        return;
    }

    i32 r2 = radius * radius;
    for (i32 y = -radius; y <= radius; y++) {
        for (i32 x = -radius; x <= radius; x++) {
            if (x * x + y * y <= r2) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void gfx_fill_circle_alpha(i32 cx, i32 cy, i32 radius, Color color, u8 alpha) {
    if (radius <= 0 || alpha == 0) {
        return;
    }

    i32 inner = (radius - 1) * (radius - 1);
    i32 outer = radius * radius;
    i32 span = outer - inner;

    for (i32 y = -radius; y <= radius; y++) {
        for (i32 x = -radius; x <= radius; x++) {
            i32 dist2 = x * x + y * y;
            if (dist2 > outer) {
                continue;
            }

            u8 coverage = 255;
            if (dist2 > inner && span > 0) {
                coverage = (u8)(((outer - dist2) * 255) / span);
            }

            u8 effective_alpha = (u8)(((u32)alpha * coverage) / 255);
            put_pixel(cx + x, cy + y, blend(get_pixel(cx + x, cy + y), color, effective_alpha));
        }
    }
}

void gfx_fill_round_rect(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    gfx_fill_round_rect_alpha(x, y, width, height, radius, color, 255);
}

void gfx_fill_round_rect_plain_alpha(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color, u8 alpha) {
    if (width <= 0 || height <= 0) {
        return;
    }

    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 coverage = round_rect_coverage(px, py, x, y, width, height, radius);
            if (coverage > 0) {
                if (coverage == 255 && alpha == 255) {
                    put_pixel(px, py, color);
                    continue;
                }
                u8 effective_alpha = (u8)(((u32)alpha * coverage) / 255);
                put_pixel(px, py, blend(get_pixel(px, py), color, effective_alpha));
            }
        }
    }
}

void gfx_fill_round_rect_alpha(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color, u8 alpha) {
    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 coverage = round_rect_coverage(px, py, x, y, width, height, radius);
            if (coverage > 0) {
                if (coverage == 255 && alpha == 255) {
                    put_pixel(px, py, color);
                    continue;
                }
                u8 effective_alpha = (u8)(((u32)alpha * coverage) / 255);
                put_pixel(px, py, blend(get_pixel(px, py), color, effective_alpha));
            }
        }
    }

    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 outer = round_rect_coverage(px, py, x, y, width, height, radius);
            if (!outer) {
                continue;
            }

            u8 inner = round_rect_coverage(px, py, x + 2, y + 2, width - 4, height - 4, radius - 2);
            if (outer > inner) {
                u8 alpha = (u8)(((u32)(outer - inner) * 190) / 255);
                put_pixel(px, py, blend(get_pixel(px, py), RGB(244, 252, 255), alpha));
            }
        }
    }
}

static u8 liquid_highlight_alpha(i32 px, i32 py, i32 cx, i32 cy, i32 rx, i32 ry, u8 peak) {
    if (rx <= 0 || ry <= 0) {
        return 0;
    }
    i32 dx = px - cx;
    i32 dy = py - cy;
    i32 v = (dx * dx * 100) / (rx * rx) + (dy * dy * 100) / (ry * ry);
    if (v >= 100) {
        return 0;
    }
    return (u8)(((100 - v) * peak) / 100);
}

void gfx_liquid_glass_rect(i32 x, i32 y, i32 width, i32 height, i32 radius) {
    if (!gfx_ready || width <= 0 || height <= 0) {
        return;
    }

    gfx_blur_round_rect(x, y, width, height, radius);
    gfx_blur_round_rect(x + 1, y + 1, width - 2, height - 2, radius - 1);
    gfx_refract_round_rect_edges(x, y, width, height, radius, 4);

    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 coverage = round_rect_coverage(px, py, x, y, width, height, radius);
            if (!coverage) {
                continue;
            }

            i32 local_y = py - y;
            i32 edge_x = px - x;
            i32 from_right = x + width - 1 - px;
            i32 edge = edge_x < from_right ? edge_x : from_right;
            i32 from_bottom = y + height - 1 - py;

            Color source = get_pixel(px, py);
            Color color = lerp_color(source, RGB(255, 255, 255), 13107);
            u8 alpha = 38;
            if (edge_x < width / 4) {
                color = lerp_color(get_pixel(x - 8, py), RGB(255, 255, 255), 16384);
                alpha = 48;
            } else if (edge_x > (width * 3) / 4) {
                color = lerp_color(get_pixel(x + width + 8, py), RGB(255, 255, 255), 9830);
                alpha = 40;
            }
            if (local_y < height / 4) {
                color = lerp_color(get_pixel(px, y - 8), RGB(255, 255, 255), 19660);
                alpha = (u8)(alpha + 8);
            } else if (local_y > height - height / 4) {
                color = lerp_color(get_pixel(px, y + height + 8), RGB(255, 255, 255), 6553);
                alpha = (u8)(alpha + 2);
            }

            u8 effective = (u8)(((u32)alpha * coverage) / 255);
            put_pixel(px, py, blend(get_pixel(px, py), color, effective));

            if (from_bottom < 12) {
                put_pixel(px, py, blend(get_pixel(px, py), RGB(0, 0, 42), (u8)((22 * coverage) / 255)));
            }
            if (edge < 3) {
                put_pixel(px, py, blend(get_pixel(px, py), RGB(218, 246, 255), (u8)(((3 - edge) * 42 * coverage) / 255)));
            }
            bool near_edge = local_y < 5 || from_bottom < 5 || edge < 5;
            if (near_edge) {
                Color ambient = get_pixel(px, py);
                if (local_y < 5) {
                    ambient = get_pixel(px, y - 7);
                } else if (from_bottom < 5) {
                    ambient = get_pixel(px, y + height + 6);
                } else if (edge_x < 5) {
                    ambient = get_pixel(x - 7, py);
                } else if (from_right < 5) {
                    ambient = get_pixel(x + width + 6, py);
                }

                i32 rim_distance = local_y < 5 ? local_y : (from_bottom < 5 ? from_bottom : edge);
                u8 rim = (u8)(((5 - rim_distance) * 18 * coverage) / 255);
                put_pixel(px, py, blend(get_pixel(px, py), ambient, rim));
                put_pixel(px, py, blend(get_pixel(px, py), RGB(238, 252, 255), (u8)((rim * 4) / 5)));
            }

            if (radius > 10 && edge < radius && (local_y < radius || from_bottom < radius)) {
                i32 cx = edge_x < radius ? radius : width - radius - 1;
                i32 cy = local_y < radius ? radius : height - radius - 1;
                i32 dx = edge_x - cx;
                i32 dy = local_y - cy;
                i32 delta = dx * dx + dy * dy - radius * radius;
                if (delta < 0) {
                    delta = -delta;
                }
                i32 band = radius * 3;
                if (delta < band) {
                    u8 arc = (u8)(((band - delta) * 68 * coverage) / (band * 255));
                    put_pixel(px, py, blend(get_pixel(px, py), RGB(244, 252, 255), arc));
                }
            }

            i32 small = width < 180 || height < 46;
            u8 h1 = liquid_highlight_alpha(px, py, x + width / 13, y + height / 5,
                                           small ? width / 5 : width / 7,
                                           small ? height / 3 : height / 2, 96);
            u8 h2 = liquid_highlight_alpha(px, py, x + (width * 79) / 100, y + height / 3,
                                           small ? width / 7 : width / 10,
                                           small ? height / 3 : (height * 7) / 10, 74);
            u8 h3 = liquid_highlight_alpha(px, py, x + width / 2, y + 3,
                                           small ? width / 3 : (width * 3) / 10,
                                           3, 16);
            u8 glow = h1 > h2 ? h1 : h2;
            glow = glow > h3 ? glow : h3;
            if (glow) {
                Color spec = h3 >= glow ? RGB(246, 252, 255) : lerp_color(get_pixel(px, py), RGB(255, 255, 255), 26214);
                put_pixel(px, py, blend(get_pixel(px, py), spec, (u8)(((u32)glow * coverage) / 255)));
            }
        }
    }
}

static i64 dist2_to_segment(i32 px, i32 py, i32 x0, i32 y0, i32 x1, i32 y1) {
    i64 vx = x1 - x0;
    i64 vy = y1 - y0;
    i64 wx = px - x0;
    i64 wy = py - y0;
    i64 len2 = vx * vx + vy * vy;
    if (len2 <= 0) {
        i64 dx = px - x0;
        i64 dy = py - y0;
        return dx * dx + dy * dy;
    }

    i64 dot = wx * vx + wy * vy;
    if (dot <= 0) {
        i64 dx = px - x0;
        i64 dy = py - y0;
        return dx * dx + dy * dy;
    }
    if (dot >= len2) {
        i64 dx = px - x1;
        i64 dy = py - y1;
        return dx * dx + dy * dy;
    }

    i32 cx = x0 + (i32)((vx * dot + len2 / 2) / len2);
    i32 cy = y0 + (i32)((vy * dot + len2 / 2) / len2);
    i64 dx = px - cx;
    i64 dy = py - cy;
    return dx * dx + dy * dy;
}

static u8 grip_path_coverage(i32 px, i32 py, i32 x, i32 y, i32 width, i32 height, i32 radius) {
    if (radius <= 0 || px < x || py < y || px >= x + width || py >= y + height) {
        return 0;
    }

    i32 x0 = x + (width * 18) / 100;
    i32 y0 = y + (height * 72) / 100;
    i32 x1 = x + (width * 45) / 100;
    i32 y1 = y + (height * 68) / 100;
    i32 x2 = x + (width * 82) / 100;
    i32 y2 = y + (height * 28) / 100;

    i64 d0 = dist2_to_segment(px, py, x0, y0, x1, y1);
    i64 d1 = dist2_to_segment(px, py, x1, y1, x2, y2);
    i64 dist2 = d0 < d1 ? d0 : d1;
    i64 inner = (i64)(radius - 1) * (radius - 1);
    i64 outer = (i64)radius * radius;
    i64 span = outer - inner;
    if (dist2 <= inner) {
        return 255;
    }
    if (dist2 > outer || span <= 0) {
        return 0;
    }
    return (u8)(((outer - dist2) * 255) / span);
}

void gfx_liquid_glass_grip(i32 x, i32 y, i32 width, i32 height, i32 radius) {
    if (!gfx_ready || width <= 0 || height <= 0) {
        return;
    }

    if (radius < 3) {
        radius = 3;
    }
    if (radius > height / 2) {
        radius = height / 2;
    }

    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 coverage = grip_path_coverage(px, py, x, y, width, height, radius);
            if (!coverage) {
                continue;
            }

            u32 r = 0;
            u32 g = 0;
            u32 b = 0;
            u32 count = 0;
            for (i32 oy = -2; oy <= 2; oy++) {
                for (i32 ox = -2; ox <= 2; ox++) {
                    Color c = get_pixel(px + ox, py + oy);
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    count++;
                }
            }

            Color blurred = RGB(r / count, g / count, b / count);
            put_pixel(px, py, blend(get_pixel(px, py), blurred, (u8)((92 * coverage) / 255)));
        }
    }

    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 coverage = grip_path_coverage(px, py, x, y, width, height, radius);
            if (!coverage) {
                continue;
            }

            u8 inner = grip_path_coverage(px, py, x, y, width, height, radius - 2);
            i32 local_y = py - y;
            u32 amount = height > 1 ? (u32)((local_y * 65536) / (height - 1)) : 0;
            Color tint = lerp_color(RGB(232, 246, 255), RGB(126, 156, 190), amount);
            put_pixel(px, py, blend(get_pixel(px, py), tint, (u8)((18 * coverage) / 255)));

            if (inner < coverage) {
                i32 dx = px < x + width / 2 ? -1 : 1;
                i32 dy = py < y + height / 2 ? -1 : 1;
                Color refracted = get_pixel(px + dx, py + dy);
                u8 edge = (u8)(coverage - inner);
                put_pixel(px, py, blend(get_pixel(px, py), refracted, (u8)((54 * edge) / 255)));
                put_pixel(px, py, blend(get_pixel(px, py), RGB(245, 252, 255), (u8)((68 * edge) / 255)));
            }
        }
    }
}

void gfx_blur_round_rect(i32 x, i32 y, i32 width, i32 height, i32 radius) {
    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            if (!inside_round_rect(px, py, x, y, width, height, radius)) {
                continue;
            }

            u32 r = 0;
            u32 g = 0;
            u32 b = 0;
            u32 count = 0;

            for (i32 oy = -2; oy <= 2; oy++) {
                for (i32 ox = -2; ox <= 2; ox++) {
                    Color c = get_pixel(px + ox, py + oy);
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    count++;
                }
            }

            put_pixel(px, py, RGB(r / count, g / count, b / count));
        }
    }
}

void gfx_refract_round_rect_edges(i32 x, i32 y, i32 width, i32 height, i32 radius, i32 strength) {
    if (!gfx_ready || width <= 0 || height <= 0 || strength <= 0) {
        return;
    }

    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 outer = round_rect_coverage(px, py, x, y, width, height, radius);
            if (!outer) {
                continue;
            }

            u8 inner = round_rect_coverage(px, py, x + 5, y + 5, width - 10, height - 10, radius - 5);
            if (inner > 0) {
                continue;
            }

            i32 dx = 0;
            i32 dy = 0;
            if (px < x + radius) {
                dx = strength;
            } else if (px >= x + width - radius) {
                dx = -strength;
            }
            if (py < y + radius) {
                dy = strength;
            } else if (py >= y + height - radius) {
                dy = -strength;
            }

            Color refracted = get_pixel(px + dx, py + dy);
            u8 alpha = (u8)(((u32)(outer - inner) * 92) / 255);
            put_pixel(px, py, blend(get_pixel(px, py), refracted, alpha));
        }
    }
}

void gfx_draw_round_rect_alpha(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color, u8 alpha) {
    if (!gfx_ready || width <= 0 || height <= 0 || alpha == 0) {
        return;
    }

    for (i32 py = y; py < y + height; py++) {
        for (i32 px = x; px < x + width; px++) {
            u8 outer = round_rect_coverage(px, py, x, y, width, height, radius);
            if (!outer) {
                continue;
            }

            u8 inner = round_rect_coverage(px, py, x + 1, y + 1, width - 2, height - 2, radius - 1);
            if (outer <= inner) {
                continue;
            }

            u8 coverage = (u8)(outer - inner);
            u8 effective = (u8)(((u32)alpha * coverage) / 255);
            put_pixel(px, py, blend(get_pixel(px, py), color, effective));
        }
    }
}

void gfx_draw_round_rect(i32 x, i32 y, i32 width, i32 height, i32 radius, Color color) {
    gfx_draw_line(x + radius, y, x + width - radius, y, color);
    gfx_draw_line(x + radius, y + height - 1, x + width - radius, y + height - 1, color);
    gfx_draw_line(x, y + radius, x, y + height - radius, color);
    gfx_draw_line(x + width - 1, y + radius, x + width - 1, y + height - radius, color);
}

void gfx_draw_rgb565_image_scaled(i32 x, i32 y, i32 width, i32 height, const u16 *pixels, u32 src_width, u32 src_height) {
    if (!pixels || width <= 0 || height <= 0 || src_width == 0 || src_height == 0) {
        return;
    }

    for (i32 py = 0; py < height; py++) {
        u32 sy = ((u32)py * src_height) / (u32)height;
        for (i32 px = 0; px < width; px++) {
            u32 sx = ((u32)px * src_width) / (u32)width;
            u16 packed = pixels[sy * src_width + sx];
            put_pixel(x + px, y + py, unpack_rgb565(packed));
        }
    }
}

void gfx_draw_argb8888_image_scaled(i32 x, i32 y, i32 width, i32 height, const u32 *pixels, u32 src_width, u32 src_height) {
    if (!pixels || width <= 0 || height <= 0 || src_width == 0 || src_height == 0) {
        return;
    }

    for (i32 py = 0; py < height; py++) {
        u32 sy = ((u32)py * src_height) / (u32)height;
        for (i32 px = 0; px < width; px++) {
            u32 sx = ((u32)px * src_width) / (u32)width;
            u32 packed = pixels[sy * src_width + sx];
            u8 alpha = (u8)(packed >> 24);
            if (alpha == 0) {
                continue;
            }

            Color color = RGB((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF);
            if (alpha == 255) {
                put_pixel(x + px, y + py, color);
            } else {
                put_pixel(x + px, y + py, blend(get_pixel(x + px, y + py), color, alpha));
            }
        }
    }
}

void gfx_draw_argb8888_image_scaled_round(i32 x, i32 y, i32 width, i32 height, i32 radius,
                                          const u32 *pixels, u32 src_width, u32 src_height) {
    if (!pixels || width <= 0 || height <= 0 || src_width == 0 || src_height == 0) {
        return;
    }

    for (i32 py = 0; py < height; py++) {
        u32 sy = ((u32)py * src_height) / (u32)height;
        for (i32 px = 0; px < width; px++) {
            u8 coverage = round_rect_coverage(x + px, y + py, x, y, width, height, radius);
            if (coverage == 0) {
                continue;
            }

            u32 sx = ((u32)px * src_width) / (u32)width;
            u32 packed = pixels[sy * src_width + sx];
            u8 alpha = (u8)(packed >> 24);
            if (alpha == 0) {
                continue;
            }

            alpha = (u8)(((u32)alpha * coverage) / 255);
            Color color = RGB((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF);
            if (alpha == 255) {
                put_pixel(x + px, y + py, color);
            } else {
                put_pixel(x + px, y + py, blend(get_pixel(x + px, y + py), color, alpha));
            }
        }
    }
}

void gfx_prepare_wallpaper_rgb565(const u16 *pixels, u32 src_width, u32 src_height) {
    if (!gfx_ready || !pixels || src_width == 0 || src_height == 0) {
        return;
    }

    u32 screen_w = active_fb->width;
    u32 screen_h = active_fb->height;
    u32 denom_x = screen_w > 1 ? screen_w - 1 : 1;
    u32 denom_y = screen_h > 1 ? screen_h - 1 : 1;
    u32 max_src_x = src_width > 1 ? src_width - 1 : 0;
    u32 max_src_y = src_height > 1 ? src_height - 1 : 0;

    for (u32 y = 0; y < active_fb->height; y++) {
        u64 sy_fixed = ((u64)y * max_src_y << 16) / denom_y;
        u32 sy = (u32)(sy_fixed >> 16);
        u32 fy = (u32)(sy_fixed & 0xFFFF);
        u32 sy_next = sy < max_src_y ? sy + 1 : sy;
        for (u32 x = 0; x < active_fb->width; x++) {
            u64 sx_fixed = ((u64)x * max_src_x << 16) / denom_x;
            u32 sx = (u32)(sx_fixed >> 16);
            u32 fx = (u32)(sx_fixed & 0xFFFF);
            u32 sx_next = sx < max_src_x ? sx + 1 : sx;

            Color top = lerp_color(
                unpack_rgb565(pixels[sy * src_width + sx]),
                unpack_rgb565(pixels[sy * src_width + sx_next]),
                fx
            );
            Color bottom = lerp_color(
                unpack_rgb565(pixels[sy_next * src_width + sx]),
                unpack_rgb565(pixels[sy_next * src_width + sx_next]),
                fx
            );
            wallpaper_buffer[y * active_fb->width + x] = lerp_color(top, bottom, fy);
        }
    }

    wallpaper_ready = true;
}

void gfx_draw_wallpaper(void) {
    if (!gfx_ready || !wallpaper_ready) {
        return;
    }

    if (!clip_enabled) {
        u32 pixels = active_fb->width * active_fb->height;
        memcpy(back_buffer, wallpaper_buffer, pixels * sizeof(u32));
        return;
    }

    for (i32 py = clip_y0; py < clip_y1; py++) {
        u32 *dest = &back_buffer[(u32)py * active_fb->width + (u32)clip_x0];
        u32 *src = &wallpaper_buffer[(u32)py * active_fb->width + (u32)clip_x0];
        memcpy(dest, src, (size_t)(clip_x1 - clip_x0) * sizeof(u32));
    }
}

static u32 ui_font_index(char ch) {
    if (ch < UI_FONT_FIRST || ch > UI_FONT_LAST) {
        ch = '?';
    }
    return (u32)ch - UI_FONT_FIRST;
}

static i32 ui_font_output_height(u32 scale) {
    return scale <= 1 ? (i32)(UI_FONT_HEIGHT / UI_FONT_NATIVE_SCALE) : (i32)UI_FONT_HEIGHT;
}

static i32 ui_font_advance(u32 index, u32 scale) {
    u32 advance = ui_font_widths[index];
    if (scale <= 1) {
        i32 compact_advance = (i32)((advance + UI_FONT_NATIVE_SCALE - 1) / UI_FONT_NATIVE_SCALE);
        return compact_advance > 3 ? compact_advance - 1 : compact_advance;
    }
    return advance > 4 ? (i32)advance - 2 : (i32)advance;
}

static i32 ui_font_percent_advance(u32 index, u32 percent) {
    u32 advance = ui_font_widths[index] > 4 ? ui_font_widths[index] - 2 : ui_font_widths[index];
    i32 scaled = (i32)((advance * percent + 50) / 100);
    return scaled > 1 ? scaled : 1;
}

static bool text_pointer_valid(const char *text) {
    uintptr_t value = (uintptr_t)text;
    return value >= 0x1000 && value < 0x40000000ULL;
}

static u8 ui_font_native_alpha(u32 index, u32 x, u32 y) {
    if (x >= UI_FONT_WIDTH || y >= UI_FONT_HEIGHT) {
        return 0;
    }

    u8 packed = ui_font_alpha[index][y][x / 2];
    u8 value = (x & 1) ? (u8)(packed & 0x0F) : (u8)(packed >> 4);
    return (u8)((value << 4) | value);
}

static u8 ui_font_downsample_alpha(u32 index, u32 dst_x, u32 dst_y) {
    u32 src_x = dst_x * UI_FONT_NATIVE_SCALE;
    u32 src_y = dst_y * UI_FONT_NATIVE_SCALE;
    u32 sum = 0;
    u32 count = 0;

    for (u32 y = 0; y < UI_FONT_NATIVE_SCALE && src_y + y < UI_FONT_HEIGHT; y++) {
        for (u32 x = 0; x < UI_FONT_NATIVE_SCALE && src_x + x < UI_FONT_WIDTH; x++) {
            sum += ui_font_native_alpha(index, src_x + x, src_y + y);
            count++;
        }
    }

    return count ? (u8)(sum / count) : 0;
}

void gfx_draw_char(i32 x, i32 y, char ch, Color color, u32 scale) {
    u32 index = ui_font_index(ch);
    u32 draw_width = ui_font_widths[index] + 2;
    if (draw_width > UI_FONT_WIDTH) {
        draw_width = UI_FONT_WIDTH;
    }

    if (scale <= 1) {
        u32 out_width = (draw_width + UI_FONT_NATIVE_SCALE - 1) / UI_FONT_NATIVE_SCALE;
        u32 out_height = UI_FONT_HEIGHT / UI_FONT_NATIVE_SCALE;
        for (u32 row = 0; row < out_height; row++) {
            for (u32 col = 0; col < out_width; col++) {
                u8 alpha = ui_font_downsample_alpha(index, col, row);
                if (alpha) {
                    put_pixel(x + (i32)col, y + (i32)row, blend(get_pixel(x + (i32)col, y + (i32)row), color, alpha));
                }
            }
        }
        return;
    }

    for (u32 row = 0; row < UI_FONT_HEIGHT; row++) {
        for (u32 col = 0; col < draw_width; col++) {
            u8 alpha = ui_font_native_alpha(index, col, row);
            if (alpha) {
                put_pixel(x + (i32)col, y + (i32)row, blend(get_pixel(x + (i32)col, y + (i32)row), color, alpha));
            }
        }
    }
}

void gfx_draw_text(i32 x, i32 y, const char *text, Color color, u32 scale) {
    if (!text_pointer_valid(text)) {
        return;
    }

    i32 cursor_x = x;
    i32 cursor_y = y;
    i32 line_height = ui_font_output_height(scale) + (scale <= 1 ? 3 : 5);

    while (*text) {
        if (*text == '\n') {
            cursor_x = x;
            cursor_y += line_height;
        } else {
            u32 index = ui_font_index(*text);
            gfx_draw_char(cursor_x, cursor_y, *text, color, scale);
            cursor_x += ui_font_advance(index, scale);
        }
        text++;
    }
}

void gfx_draw_text_percent(i32 x, i32 y, const char *text, Color color, u32 percent) {
    if (!text_pointer_valid(text)) {
        return;
    }

    if (percent < 35) {
        percent = 35;
    }

    i32 cursor_x = x;
    while (*text) {
        if (*text == '\n') {
            cursor_x = x;
        } else {
            u32 index = ui_font_index(*text);
            u32 draw_width = ui_font_widths[index] + 2;
            if (draw_width > UI_FONT_WIDTH) {
                draw_width = UI_FONT_WIDTH;
            }

            u32 out_width = (draw_width * percent + 50) / 100;
            u32 out_height = (UI_FONT_HEIGHT * percent + 50) / 100;
            if (out_width < 1) {
                out_width = 1;
            }
            if (out_height < 1) {
                out_height = 1;
            }

            for (u32 row = 0; row < out_height; row++) {
                u32 src_y = (row * 100) / percent;
                if (src_y >= UI_FONT_HEIGHT) {
                    src_y = UI_FONT_HEIGHT - 1;
                }
                for (u32 col = 0; col < out_width; col++) {
                    u32 src_x = (col * 100) / percent;
                    if (src_x >= draw_width) {
                        src_x = draw_width - 1;
                    }
                    u8 alpha = ui_font_native_alpha(index, src_x, src_y);
                    if (alpha) {
                        put_pixel(cursor_x + (i32)col, y + (i32)row,
                                  blend(get_pixel(cursor_x + (i32)col, y + (i32)row), color, alpha));
                    }
                }
            }
            cursor_x += ui_font_percent_advance(index, percent);
        }
        text++;
    }
}

static bool cursor_color_at(i32 cursor_x, i32 cursor_y, i32 px, i32 py, Color *color) {
    i32 local_x = px - cursor_x;
    i32 local_y = py - cursor_y;
    if (local_x < 0 || local_y < 0 || local_x >= CURSOR_WIDTH || local_y >= CURSOR_HEIGHT) {
        return false;
    }

    u32 packed = cursor_pixels[(u32)local_y * CURSOR_WIDTH + (u32)local_x];
    u8 alpha = (u8)(packed >> 24);
    if (alpha == 0) {
        return false;
    }

    Color top = packed & 0xFFFFFF;
    *color = alpha == 255 ? top : blend(*color, top, alpha);
    return true;
}

void gfx_draw_cursor(i32 x, i32 y) {
    for (u32 row = 0; row < CURSOR_HEIGHT; row++) {
        for (u32 col = 0; col < CURSOR_WIDTH; col++) {
            Color color;
            color = get_pixel(x + (i32)col, y + (i32)row);
            if (cursor_color_at(x, y, x + (i32)col, y + (i32)row, &color)) {
                put_pixel(x + (i32)col, y + (i32)row, color);
            }
        }
    }
}

void gfx_save_scene(void) {
    if (!gfx_ready) {
        return;
    }

    u32 pixels = active_fb->width * active_fb->height;
    memcpy(scene_buffer, back_buffer, pixels * sizeof(u32));
}

void gfx_restore_scene_rect(i32 x, i32 y, i32 width, i32 height) {
    if (!gfx_ready || width <= 0 || height <= 0) {
        return;
    }

    i32 x0 = x < 0 ? 0 : x;
    i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + width;
    i32 y1 = y + height;

    if (x1 > (i32)active_fb->width) {
        x1 = (i32)active_fb->width;
    }
    if (y1 > (i32)active_fb->height) {
        y1 = (i32)active_fb->height;
    }

    for (i32 py = y0; py < y1; py++) {
        u32 *dest = &back_buffer[(u32)py * active_fb->width + (u32)x0];
        u32 *src = &scene_buffer[(u32)py * active_fb->width + (u32)x0];
        memcpy(dest, src, (size_t)(x1 - x0) * sizeof(u32));
    }
}

void gfx_present_rect(i32 x, i32 y, i32 width, i32 height) {
    if (!gfx_ready || width <= 0 || height <= 0) {
        return;
    }

    i32 x0 = x < 0 ? 0 : x;
    i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + width;
    i32 y1 = y + height;

    if (x1 > (i32)active_fb->width) {
        x1 = (i32)active_fb->width;
    }
    if (y1 > (i32)active_fb->height) {
        y1 = (i32)active_fb->height;
    }

    for (i32 py = y0; py < y1; py++) {
        u8 *dest = active_fb->address + ((u64)py * active_fb->pitch);
        u32 *src = &back_buffer[(u32)py * active_fb->width];

        if (active_fb->bpp == 32) {
            memcpy(&((u32 *)dest)[x0], &src[x0], (size_t)(x1 - x0) * sizeof(u32));
        } else {
            for (i32 px = x0; px < x1; px++) {
                Color color = src[px];
                dest[px * 3 + 0] = (u8)(color & 0xFF);
                dest[px * 3 + 1] = (u8)((color >> 8) & 0xFF);
                dest[px * 3 + 2] = (u8)((color >> 16) & 0xFF);
            }
        }
    }
}

void gfx_present(void) {
    if (!gfx_ready) {
        return;
    }
    gfx_present_rect(0, 0, (i32)active_fb->width, (i32)active_fb->height);
}

void gfx_present_cursor(i32 previous_x, i32 previous_y, i32 cursor_x, i32 cursor_y) {
    if (!gfx_ready) {
        return;
    }

    i32 x0 = previous_x < cursor_x ? previous_x : cursor_x;
    i32 y0 = previous_y < cursor_y ? previous_y : cursor_y;
    i32 x1 = previous_x > cursor_x ? previous_x : cursor_x;
    i32 y1 = previous_y > cursor_y ? previous_y : cursor_y;

    x0 -= 2;
    y0 -= 2;
    x1 += CURSOR_WIDTH + 2;
    y1 += CURSOR_HEIGHT + 2;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (i32)active_fb->width) {
        x1 = (i32)active_fb->width;
    }
    if (y1 > (i32)active_fb->height) {
        y1 = (i32)active_fb->height;
    }

    for (i32 py = y0; py < y1; py++) {
        u8 *dest = active_fb->address + ((u64)py * active_fb->pitch);
        u32 *src = &back_buffer[(u32)py * active_fb->width];

        for (i32 px = x0; px < x1; px++) {
            Color color = src[px];
            (void)cursor_color_at(cursor_x, cursor_y, px, py, &color);

            if (active_fb->bpp == 32) {
                ((u32 *)dest)[px] = color;
            } else {
                dest[px * 3 + 0] = (u8)(color & 0xFF);
                dest[px * 3 + 1] = (u8)((color >> 8) & 0xFF);
                dest[px * 3 + 2] = (u8)((color >> 16) & 0xFF);
            }
        }
    }
}
