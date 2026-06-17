#include <liquidos/fs.h>
#include <liquidos/app_store.h>
#include <liquidos/gfx.h>
#include <liquidos/input.h>
#include <liquidos/io.h>
#include <liquidos/lib.h>
#include <liquidos/liqueia.h>
#include <liquidos/platform.h>
#include <liquidos/terminal.h>
#include <liquidos/ui.h>
#include "../gfx/app_icons.h"
#include "../gfx/background_image.h"
#include "../gfx/liquidos_logo.h"
#include "../gfx/system_icons.h"

typedef enum WindowKind {
    WINDOW_TERMINAL = 0,
    WINDOW_BROWSER = 1,
    WINDOW_FILES = 2,
    WINDOW_STORE = 3,
    WINDOW_SETTINGS = 4,
    WINDOW_LAUNCHER = 5,
    WINDOW_APP_VIEW = 6,
    WINDOW_COUNT = 7
} WindowKind;

typedef struct Window {
    i32 x;
    i32 y;
    i32 width;
    i32 height;
    const char *title;
    bool open;
    bool expanded;
} Window;

typedef struct TaskbarLayout {
    i32 x;
    i32 y;
    i32 w;
    i32 h;
    i32 volume_x;
    i32 wifi_x;
    i32 slot_x;
    i32 slot_y;
    i32 slot_size;
    i32 slot_step;
    i32 slots_available;
    i32 search_x;
    i32 search_y;
    i32 search_w;
    i32 search_h;
} TaskbarLayout;

typedef struct DesktopTheme {
    const char *name;
    Color wash_top;
    Color wash_bottom;
    Color accent;
    Color panel;
    Color text;
} DesktopTheme;

static const DesktopTheme themes[] = {
    { "Liquid Gold", RGB(255, 208, 96), RGB(24, 18, 34), RGB(244, 184, 64), RGB(248, 245, 235), RGB(31, 34, 42) },
    { "Aurora Blue", RGB(70, 198, 255), RGB(13, 25, 59), RGB(104, 207, 255), RGB(236, 246, 255), RGB(24, 36, 56) },
    { "Glass Mint", RGB(126, 244, 194), RGB(14, 44, 50), RGB(87, 220, 170), RGB(239, 252, 248), RGB(23, 50, 47) },
    { "Night Violet", RGB(180, 116, 255), RGB(20, 16, 42), RGB(190, 136, 255), RGB(245, 240, 255), RGB(38, 28, 60) },
};

static Window windows[WINDOW_COUNT];
static WindowKind z_order[WINDOW_COUNT];
static WindowKind focused_window = WINDOW_TERMINAL;
static i32 mouse_x = 320;
static i32 mouse_y = 240;
static bool previous_left = false;
static bool dragging = false;
static bool resizing = false;
static WindowKind dragged_window = WINDOW_TERMINAL;
static WindowKind resized_window = WINDOW_TERMINAL;
static i32 drag_offset_x = 0;
static i32 drag_offset_y = 0;
static u64 last_clock_tick = 0;
static char clock_text[6] = "00:00";
static char date_text[9] = "00/00/00";
static char taskbar_message[32] = "SEARCH";
static char app_status_text[64] = "Install an app, then run it from Store or Launch Apps.";
static bool wifi_enabled = true;
static bool full_redraw_needed = true;
static bool cursor_redraw_needed = true;
static bool dirty_region_valid = false;
static i32 dirty_x0 = 0;
static i32 dirty_y0 = 0;
static i32 dirty_x1 = 0;
static i32 dirty_y1 = 0;
static i32 previous_mouse_x = 320;
static i32 previous_mouse_y = 240;
static i32 hover_zone = -1;
static i32 selected_file_index = 0;
static u32 new_file_counter = 1;
static size_t current_theme = 0;
static size_t selected_store_app = 0;

static void append_text(char *dest, size_t dest_size, const char *src);

static i32 taskbar_x(void) {
    i32 screen_w = (i32)gfx_width();
    i32 margin = screen_w / 100;
    if (margin < 16) {
        margin = 16;
    }
    if (margin > 24) {
        margin = 24;
    }
    return margin;
}

static i32 taskbar_y(void) {
    return gfx_width() < 900 ? 14 : 18;
}

static i32 taskbar_w(void) {
    i32 screen_w = (i32)gfx_width();
    return screen_w - taskbar_x() * 2;
}

static i32 taskbar_h(void) {
    return gfx_width() < 900 ? 70 : 82;
}

static void taskbar_layout(TaskbarLayout *layout) {
    i32 compact = gfx_width() < 900 ? 1 : 0;

    layout->x = taskbar_x();
    layout->y = taskbar_y();
    layout->w = taskbar_w();
    layout->h = taskbar_h();

    layout->slot_size = compact ? 48 : 58;
    layout->slot_step = compact ? 70 : 86;
    layout->slots_available = 4;
    layout->search_w = compact ? 230 : 332;
    layout->search_h = compact ? 42 : 58;
    layout->search_y = compact ? layout->y + (layout->h - layout->search_h) / 2 : layout->y + 10;

    i32 trailing_w = compact ? 12 : 14;
    layout->search_x = layout->x + layout->w - layout->search_w - trailing_w;
    layout->volume_x = 0;
    layout->wifi_x = layout->x + (compact ? 142 : 205);
    layout->slot_y = layout->y + (layout->h - layout->slot_size) / 2;

    i32 app_group_w = (layout->slots_available - 1) * layout->slot_step + layout->slot_size;
    layout->slot_x = ((i32)gfx_width() - app_group_w) / 2;
}

static bool point_in_rect(i32 px, i32 py, i32 x, i32 y, i32 width, i32 height) {
    return px >= x && py >= y && px < x + width && py < y + height;
}

static void mark_dirty_rect(i32 x, i32 y, i32 width, i32 height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    i32 x0 = x - 12;
    i32 y0 = y - 12;
    i32 x1 = x + width + 12;
    i32 y1 = y + height + 12;
    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > screen_w) {
        x1 = screen_w;
    }
    if (y1 > screen_h) {
        y1 = screen_h;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    if (!dirty_region_valid) {
        dirty_x0 = x0;
        dirty_y0 = y0;
        dirty_x1 = x1;
        dirty_y1 = y1;
        dirty_region_valid = true;
    } else {
        if (x0 < dirty_x0) {
            dirty_x0 = x0;
        }
        if (y0 < dirty_y0) {
            dirty_y0 = y0;
        }
        if (x1 > dirty_x1) {
            dirty_x1 = x1;
        }
        if (y1 > dirty_y1) {
            dirty_y1 = y1;
        }
    }
    full_redraw_needed = true;
}

static void mark_dirty_full(void) {
    dirty_x0 = 0;
    dirty_y0 = 0;
    dirty_x1 = (i32)gfx_width();
    dirty_y1 = (i32)gfx_height();
    dirty_region_valid = true;
    full_redraw_needed = true;
}

static void mark_dirty_taskbar(void) {
    mark_dirty_rect(taskbar_x(), taskbar_y(), taskbar_w(), taskbar_h());
}

static void mark_dirty_window(WindowKind kind) {
    Window *window = &windows[kind];
    mark_dirty_rect(window->x, window->y, window->width, window->height);
}

static void clamp_window_size(Window *window) {
    if (window->width < 300) {
        window->width = 300;
    }
    if (window->height < 190) {
        window->height = 190;
    }
    if (window->width > (i32)gfx_width() - 40) {
        window->width = (i32)gfx_width() - 40;
    }
    if (window->height > (i32)gfx_height() - taskbar_y() - taskbar_h() - 30) {
        window->height = (i32)gfx_height() - taskbar_y() - taskbar_h() - 30;
    }
}

static void bring_to_front(WindowKind kind) {
    WindowKind old_focus = focused_window;
    focused_window = kind;
    i32 top = WINDOW_COUNT - 1;

    if (z_order[top] == kind) {
        if (old_focus != kind) {
            mark_dirty_full();
        }
        return;
    }

    i32 found = -1;
    for (i32 i = 0; i < WINDOW_COUNT; i++) {
        if (z_order[i] == kind) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return;
    }

    for (i32 i = found; i < top; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_order[top] = kind;
    mark_dirty_full();
}

static void open_window(WindowKind kind) {
    windows[kind].open = true;
    bring_to_front(kind);
    mark_dirty_window(kind);
}

static void set_taskbar_message(const char *message) {
    strncpy(taskbar_message, message, sizeof(taskbar_message) - 1);
    taskbar_message[sizeof(taskbar_message) - 1] = 0;
    mark_dirty_taskbar();
}

static void set_app_status(const char *message) {
    strncpy(app_status_text, message, sizeof(app_status_text) - 1);
    app_status_text[sizeof(app_status_text) - 1] = 0;
}

static void launch_store_app(size_t index) {
    const StoreApp *app = app_store_get(index);
    LoadResult result = app_store_launch_by_index(index);
    if (result.ok) {
        char pid_text[16];
        char message[64];
        u64_to_dec(result.pid, pid_text, sizeof(pid_text));
        message[0] = 0;
        append_text(message, sizeof(message), app ? app->display_name : "App");
        append_text(message, sizeof(message), " ran pid ");
        append_text(message, sizeof(message), pid_text);
        set_app_status(message);
        set_taskbar_message("APP RAN");
    } else {
        char message[64];
        message[0] = 0;
        append_text(message, sizeof(message), "Launch failed: ");
        append_text(message, sizeof(message), result.message);
        set_app_status(message);
        set_taskbar_message("APP FAILED");
    }
    mark_dirty_window(WINDOW_STORE);
    mark_dirty_window(WINDOW_LAUNCHER);
    mark_dirty_window(WINDOW_APP_VIEW);
    mark_dirty_window(WINDOW_SETTINGS);
}

static i32 taskbar_hover_zone_at(i32 px, i32 py) {
    TaskbarLayout bar;
    taskbar_layout(&bar);

    if (!point_in_rect(px, py, bar.x, bar.y, bar.w, bar.h)) {
        return -1;
    }
    if (point_in_rect(px, py, bar.wifi_x - 10, bar.y + 22, 54, 58)) {
        return 2;
    }
    for (i32 i = 0; i < bar.slots_available; i++) {
        if (point_in_rect(px, py, bar.slot_x + i * bar.slot_step, bar.slot_y, bar.slot_size, bar.slot_size)) {
            return 10 + i;
        }
    }
    if (point_in_rect(px, py, bar.search_x, bar.search_y, bar.search_w, bar.search_h)) {
        return 30;
    }
    return 0;
}

static void clamp_window(Window *window) {
    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();
    i32 top_reserved = taskbar_y() + taskbar_h() + 10;

    clamp_window_size(window);

    if (window->x < 0) {
        window->x = 0;
    }
    if (window->y < top_reserved) {
        window->y = top_reserved;
    }
    if (window->x + window->width > screen_w) {
        window->x = screen_w - window->width;
    }
    if (window->y + window->height > screen_h) {
        window->y = screen_h - window->height;
    }
}

static void toggle_window_size(Window *window) {
    mark_dirty_rect(window->x, window->y, window->width, window->height);
    if (!window->expanded) {
        window->x = 90;
        window->y = taskbar_y() + taskbar_h() + 28;
        window->width = (i32)gfx_width() - 180;
        window->height = (i32)gfx_height() - window->y - 70;
        window->expanded = true;
    } else {
        window->width = 620;
        window->height = 360;
        window->expanded = false;
    }
    clamp_window(window);
    mark_dirty_rect(window->x, window->y, window->width, window->height);
}

static void handle_taskbar_click(void) {
    TaskbarLayout bar;
    taskbar_layout(&bar);

    if (point_in_rect(mouse_x, mouse_y, bar.wifi_x - 10, bar.y + 22, 54, 58)) {
        wifi_enabled = !wifi_enabled;
        set_taskbar_message(wifi_enabled ? "WIFI ENABLED" : "WIFI DISABLED");
    } else if (point_in_rect(mouse_x, mouse_y, bar.slot_x, bar.slot_y, bar.slot_size, bar.slot_size)) {
        open_window(WINDOW_BROWSER);
        set_taskbar_message("BROWSER");
    } else if (point_in_rect(mouse_x, mouse_y, bar.slot_x + bar.slot_step, bar.slot_y, bar.slot_size, bar.slot_size)) {
        open_window(WINDOW_FILES);
        set_taskbar_message("FILES");
    } else if (point_in_rect(mouse_x, mouse_y, bar.slot_x + bar.slot_step * 2, bar.slot_y, bar.slot_size, bar.slot_size)) {
        open_window(WINDOW_STORE);
        set_taskbar_message("STORE");
    } else if (point_in_rect(mouse_x, mouse_y, bar.slot_x + bar.slot_step * 3, bar.slot_y, bar.slot_size, bar.slot_size)) {
        open_window(WINDOW_SETTINGS);
        set_taskbar_message("PERSONALIZE");
    } else if (point_in_rect(mouse_x, mouse_y, bar.search_x, bar.search_y, bar.search_w, bar.search_h)) {
        open_window(WINDOW_LAUNCHER);
        set_taskbar_message("APP LAUNCHER");
    }
}

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
}

static void make_untitled_name(char *out, size_t out_size) {
    char number[16];
    u64_to_dec(new_file_counter++, number, sizeof(number));
    out[0] = 0;
    append_text(out, out_size, "DESKTOP/UNTITLED");
    append_text(out, out_size, number);
    append_text(out, out_size, ".TXT");
}

static void handle_files_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 width = window->width - 12;

    if (point_in_rect(mouse_x, mouse_y, x + 10, y + 10, 74, 28)) {
        char name[FS_NAME_LENGTH];
        make_untitled_name(name, sizeof(name));
        if (fs_write(name, "New LiquidOS file. Edit it from Terminal with: write NAME text")) {
            selected_file_index = fs_find_index(name);
            set_taskbar_message("FILE CREATED");
            mark_dirty_rect(window->x, window->y, window->width, window->height);
        }
        return;
    }

    if (point_in_rect(mouse_x, mouse_y, x + 92, y + 10, 86, 28)) {
        const FsFile *file = fs_get_file((size_t)selected_file_index);
        if (file && fs_delete(file->name)) {
            if (selected_file_index >= (i32)fs_file_count()) {
                selected_file_index = (i32)fs_file_count() - 1;
            }
            if (selected_file_index < 0) {
                selected_file_index = 0;
            }
            set_taskbar_message("FILE DELETED");
            mark_dirty_rect(window->x, window->y, window->width, window->height);
        }
        return;
    }

    if (point_in_rect(mouse_x, mouse_y, x + 188, y + 10, 112, 28)) {
        fs_write("DESKTOP/DEMO.TXT", "Saved from the graphical Files app. This file lives in the LiquidOS RAM filesystem.");
        selected_file_index = fs_find_index("DESKTOP/DEMO.TXT");
        set_taskbar_message("FILE SAVED");
        mark_dirty_rect(window->x, window->y, window->width, window->height);
        return;
    }

    i32 row_y = y + 58;
    i32 row_h = 25;
    i32 list_w = width / 2 - 16;
    if (point_in_rect(mouse_x, mouse_y, x + 10, row_y, list_w, row_h * (i32)fs_file_count())) {
        i32 row = (mouse_y - row_y) / row_h;
        if (row >= 0 && row < (i32)fs_file_count()) {
            selected_file_index = row;
            set_taskbar_message("FILE SELECTED");
            mark_dirty_rect(window->x, window->y, window->width, window->height);
        }
    }
}

static void handle_browser_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 width = window->width - 12;
    i32 height = window->height - 36;

    liqueia_handle_click(x, y, width, height, mouse_x, mouse_y);
    set_taskbar_message("LIQUEIA");
    mark_dirty_rect(window->x, window->y, window->width, window->height);
}

static void handle_store_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 card_y = y + 68;

    for (size_t i = 0; i < app_store_count(); i++) {
        i32 row_y = card_y + (i32)i * 86;
        bool installed = app_store_is_installed(i);
        i32 primary_x = x + window->width - 190;
        i32 remove_x = x + window->width - 108;
        if (point_in_rect(mouse_x, mouse_y, primary_x, row_y + 22, 76, 30)) {
            if (installed) {
                selected_store_app = i;
                launch_store_app(i);
            } else if (app_store_install_by_index(i)) {
                const StoreApp *app = app_store_get(i);
                char message[64];
                message[0] = 0;
                append_text(message, sizeof(message), app ? app->display_name : "App");
                append_text(message, sizeof(message), " installed.");
                set_app_status(message);
                set_taskbar_message("APP INSTALLED");
                mark_dirty_window(WINDOW_LAUNCHER);
                mark_dirty_window(WINDOW_SETTINGS);
            } else {
                StoreInstallCheck check = app_store_validate_by_index(i);
                set_app_status(check.message);
                set_taskbar_message("INSTALL FAILED");
            }
            mark_dirty_rect(window->x, window->y, window->width, window->height);
            return;
        }
        if (installed && point_in_rect(mouse_x, mouse_y, remove_x, row_y + 22, 78, 30)) {
            if (app_store_uninstall_by_index(i)) {
                set_app_status("App removed.");
                set_taskbar_message("APP REMOVED");
            } else {
                set_app_status("Remove failed.");
                set_taskbar_message("REMOVE FAILED");
            }
            mark_dirty_rect(window->x, window->y, window->width, window->height);
            mark_dirty_window(WINDOW_LAUNCHER);
            mark_dirty_window(WINDOW_SETTINGS);
            mark_dirty_window(WINDOW_APP_VIEW);
            return;
        }
    }
}

static void handle_settings_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 start_y = y + 78;
    i32 theme_w = window->width / 2 - 38;

    for (size_t i = 0; i < sizeof(themes) / sizeof(themes[0]); i++) {
        i32 row_y = start_y + 18 + (i32)i * 46;
        if (point_in_rect(mouse_x, mouse_y, x + 18, row_y, theme_w, 36)) {
            current_theme = i;
            set_taskbar_message(themes[i].name);
            char theme_id[2];
            theme_id[0] = (char)('0' + i);
            theme_id[1] = 0;
            fs_write("SYSTEM/THEME.TXT", theme_id);
            mark_dirty_full();
            return;
        }
    }

    i32 manager_x = x + window->width / 2 + 6;
    i32 manager_w = window->width / 2 - 34;
    i32 row_y = y + 100;
    for (size_t i = 0; i < app_store_count(); i++) {
        if (!app_store_is_installed(i)) {
            continue;
        }
        if (point_in_rect(mouse_x, mouse_y, manager_x + manager_w - 112, row_y + 5, 44, 24)) {
            selected_store_app = i;
            launch_store_app(i);
            return;
        }
        if (point_in_rect(mouse_x, mouse_y, manager_x + manager_w - 62, row_y + 5, 54, 24)) {
            if (app_store_uninstall_by_index(i)) {
                set_app_status("App removed from App Manager.");
                set_taskbar_message("APP REMOVED");
            } else {
                set_app_status("Remove failed.");
                set_taskbar_message("REMOVE FAILED");
            }
            mark_dirty_full();
            return;
        }
        row_y += 38;
    }
}

static void load_theme_setting(void) {
    const FsFile *theme_file = fs_find("SYSTEM/THEME.TXT");
    if (!theme_file || theme_file->size == 0) {
        return;
    }

    char selected = theme_file->contents[0];
    if (selected >= '0' && selected < (char)('0' + (sizeof(themes) / sizeof(themes[0])))) {
        current_theme = (size_t)(selected - '0');
    }
}

static void handle_launcher_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 tile_y = y + 72;

    for (i32 i = 0; i < 3; i++) {
        i32 tile_x = x + 22 + i * 132;
        if (point_in_rect(mouse_x, mouse_y, tile_x, tile_y, 112, 82)) {
            if (i == 0) {
                open_window(WINDOW_BROWSER);
            } else if (i == 1) {
                open_window(WINDOW_FILES);
            } else {
                open_window(WINDOW_TERMINAL);
            }
            set_taskbar_message("APP OPENED");
            return;
        }
    }

    i32 installed_y = tile_y + 122;
    size_t visible = 0;
    for (size_t i = 0; i < app_store_count(); i++) {
        if (!app_store_is_installed(i)) {
            continue;
        }
        i32 row_y = installed_y + (i32)visible * 44;
        if (point_in_rect(mouse_x, mouse_y, x + 22, row_y, window->width - 56, 36)) {
            selected_store_app = i;
            launch_store_app(i);
            return;
        }
        visible++;
    }
}

static void handle_app_view_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 width = window->width - 12;

    if (point_in_rect(mouse_x, mouse_y, x + width - 166, y + 22, 62, 28)) {
        launch_store_app(selected_store_app);
        return;
    }
    if (point_in_rect(mouse_x, mouse_y, x + width - 96, y + 22, 72, 28)) {
        if (app_store_uninstall_by_index(selected_store_app)) {
            set_app_status("App removed.");
            set_taskbar_message("APP REMOVED");
        } else {
            set_app_status("Remove failed.");
            set_taskbar_message("REMOVE FAILED");
        }
        mark_dirty_full();
    }
}

static void handle_window_click(void) {
    for (int z = WINDOW_COUNT - 1; z >= 0; z--) {
        WindowKind kind = z_order[z];
        Window *window = &windows[kind];

        if (!window->open) {
            continue;
        }

        if (!point_in_rect(mouse_x, mouse_y, window->x, window->y, window->width, window->height)) {
            continue;
        }

        bring_to_front(kind);

        if (point_in_rect(mouse_x, mouse_y, window->x + 10, window->y + 7, 12, 12)) {
            mark_dirty_rect(window->x, window->y, window->width, window->height);
            window->open = false;
            return;
        }

        if (point_in_rect(mouse_x, mouse_y, window->x + 30, window->y + 7, 12, 12)) {
            toggle_window_size(window);
            return;
        }

        if (point_in_rect(mouse_x, mouse_y, window->x + window->width - 18, window->y + window->height - 18, 18, 18)) {
            resizing = true;
            resized_window = kind;
            return;
        }

        if (point_in_rect(mouse_x, mouse_y, window->x, window->y, window->width, 24)) {
            dragging = true;
            dragged_window = kind;
            drag_offset_x = mouse_x - window->x;
            drag_offset_y = mouse_y - window->y;
            return;
        }

        if (point_in_rect(mouse_x, mouse_y, window->x + 6, window->y + 30, window->width - 12, window->height - 36)) {
            if (kind == WINDOW_FILES) {
                handle_files_click(window);
            } else if (kind == WINDOW_BROWSER) {
                handle_browser_click(window);
            } else if (kind == WINDOW_STORE) {
                handle_store_click(window);
            } else if (kind == WINDOW_SETTINGS) {
                handle_settings_click(window);
            } else if (kind == WINDOW_LAUNCHER) {
                handle_launcher_click(window);
            } else if (kind == WINDOW_APP_VIEW) {
                handle_app_view_click(window);
            }
        }

        return;
    }
}

static u8 bcd_to_binary(u8 value) {
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

static u8 cmos_read(u8 reg) {
    outb(0x70, (u8)(0x80 | reg));
    return inb(0x71);
}

static void update_clock_text(void) {
    u8 status_b = cmos_read(0x0B);
    u8 minute = cmos_read(0x02);
    u8 hour = cmos_read(0x04);
    u8 day = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year = cmos_read(0x09);

    if ((status_b & 0x04) == 0) {
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }

    hour %= 24;
    minute %= 60;

    clock_text[0] = (char)('0' + hour / 10);
    clock_text[1] = (char)('0' + hour % 10);
    clock_text[2] = ':';
    clock_text[3] = (char)('0' + minute / 10);
    clock_text[4] = (char)('0' + minute % 10);
    clock_text[5] = 0;

    day = day == 0 ? 1 : day;
    month = month == 0 ? 1 : month;
    date_text[0] = (char)('0' + (day % 100) / 10);
    date_text[1] = (char)('0' + day % 10);
    date_text[2] = '/';
    date_text[3] = (char)('0' + (month % 100) / 10);
    date_text[4] = (char)('0' + month % 10);
    date_text[5] = '/';
    date_text[6] = (char)('0' + (year % 100) / 10);
    date_text[7] = (char)('0' + year % 10);
    date_text[8] = 0;
}

static void draw_app_slot(i32 x, i32 y, i32 size, bool active, bool hovered) {
    if (hovered || active) {
        gfx_fill_round_rect_alpha(x + 1, y + 1, size - 2, size - 2, size / 3, RGB(210, 242, 255), hovered ? 24 : 14);
    }
    if (hovered || active) {
        gfx_draw_round_rect(x + 4, y + 4, size - 8, size - 8, size / 4, active ? RGB(204, 241, 255) : RGB(238, 248, 255));
        gfx_fill_round_rect_alpha(x + size / 3, y + size - 4, size / 3, 3, 2, active ? RGB(164, 235, 255) : RGB(255, 255, 255), active ? 210 : 105);
    }
}

static void draw_background(void) {
    const DesktopTheme *theme = &themes[current_theme];
    gfx_draw_wallpaper();
    gfx_fill_round_rect_plain_alpha(-80, -60, (i32)gfx_width() + 160, (i32)gfx_height() / 2, 0, theme->wash_top, 26);
    gfx_fill_round_rect_plain_alpha(-80, (i32)gfx_height() / 2, (i32)gfx_width() + 160, (i32)gfx_height() / 2 + 90, 0, theme->wash_bottom, 34);
    gfx_fill_circle_alpha((i32)gfx_width() - 120, 150, 160, theme->accent, 42);
    gfx_fill_circle_alpha(120, (i32)gfx_height() - 120, 140, theme->wash_top, 34);
}

static void draw_window_frame(const Window *window, bool focused) {
    const DesktopTheme *theme = &themes[current_theme];
    Color shadow = RGB(6, 9, 13);
    Color body = theme->panel;
    Color title = focused ? theme->text : RGB(66, 75, 84);
    Color border = focused ? theme->accent : RGB(120, 132, 140);

    gfx_fill_round_rect_alpha(window->x + 8, window->y + 10, window->width, window->height, 12, shadow, 110);
    if (focused) {
        gfx_fill_round_rect_alpha(window->x - 4, window->y - 4, window->width + 8, window->height + 8, 16, theme->accent, 30);
    }
    gfx_fill_round_rect(window->x, window->y, window->width, window->height, 12, body);
    gfx_draw_round_rect(window->x, window->y, window->width, window->height, 12, border);
    gfx_fill_round_rect(window->x + 1, window->y + 1, window->width - 2, 28, 11, title);
    gfx_draw_text(window->x + 58, window->y + 8, window->title, RGB(248, 250, 252), 1);

    gfx_fill_circle(window->x + 16, window->y + 13, 6, RGB(238, 94, 88));
    gfx_fill_circle(window->x + 36, window->y + 13, 6, RGB(83, 196, 101));
    gfx_draw_line(window->x + 13, window->y + 10, window->x + 19, window->y + 16, RGB(99, 28, 27));
    gfx_draw_line(window->x + 19, window->y + 10, window->x + 13, window->y + 16, RGB(99, 28, 27));
    gfx_draw_line(window->x + 33, window->y + 16, window->x + 39, window->y + 10, RGB(31, 92, 40));
    gfx_draw_line(window->x + 34, window->y + 10, window->x + 39, window->y + 10, RGB(31, 92, 40));
    gfx_draw_line(window->x + 39, window->y + 10, window->x + 39, window->y + 15, RGB(31, 92, 40));
    gfx_draw_line(window->x + window->width - 17, window->y + window->height - 6, window->x + window->width - 6, window->y + window->height - 17, RGB(132, 146, 154));
    gfx_draw_line(window->x + window->width - 12, window->y + window->height - 5, window->x + window->width - 5, window->y + window->height - 12, RGB(132, 146, 154));
}

static void draw_button(i32 x, i32 y, i32 width, const char *label, bool active) {
    gfx_fill_round_rect_alpha(x, y, width, 28, 10, active ? RGB(216, 225, 255) : RGB(250, 252, 255), active ? 230 : 195);
    gfx_draw_text(x + 12, y + 7, label, RGB(43, 50, 58), 1);
}

static void draw_file_explorer(i32 x, i32 y, i32 width, i32 height) {
    gfx_fill_rect(x, y, width, height, RGB(244, 247, 250));
    gfx_fill_rect(x, y, width, 48, RGB(229, 235, 241));
    draw_button(x + 10, y + 10, 74, "NEW", false);
    draw_button(x + 92, y + 10, 86, "DELETE", false);
    draw_button(x + 188, y + 10, 112, "SAVE DEMO", false);

    i32 list_w = width / 2 - 16;
    i32 preview_x = x + list_w + 22;
    i32 row_y = y + 58;
    i32 row_h = 25;
    gfx_draw_text(x + 12, y + 42, "RAM FILESYSTEM", RGB(77, 88, 96), 1);

    size_t count = fs_file_count();
    if (selected_file_index >= (i32)count) {
        selected_file_index = (i32)count - 1;
    }
    if (selected_file_index < 0) {
        selected_file_index = 0;
    }

    for (size_t i = 0; i < count; i++) {
        const FsFile *file = fs_get_file(i);
        if (!file) {
            continue;
        }

        bool selected = (i32)i == selected_file_index;
        Color row = selected ? RGB(208, 224, 246) : ((i & 1) == 0 ? RGB(236, 241, 245) : RGB(245, 248, 250));
        gfx_fill_round_rect_alpha(x + 10, row_y - 3, list_w, row_h - 3, 7, row, selected ? 240 : 220);

        char size_text[24];
        u64_to_dec(file->size, size_text, sizeof(size_text));
        gfx_draw_text(x + 20, row_y + 2, file->name, selected ? RGB(22, 55, 94) : RGB(44, 53, 60), 1);
        gfx_draw_text(x + list_w - 54, row_y + 2, size_text, RGB(101, 113, 121), 1);
        row_y += row_h;
    }

    gfx_fill_round_rect_alpha(preview_x, y + 58, width - list_w - 34, height - 72, 12, RGB(255, 255, 255), 230);
    const FsFile *selected = fs_get_file((size_t)selected_file_index);
    if (selected) {
        gfx_draw_text(preview_x + 14, y + 74, selected->name, RGB(35, 43, 50), 1);
        gfx_fill_rect(preview_x + 14, y + 96, width - list_w - 62, 1, RGB(218, 226, 232));
        gfx_draw_text(preview_x + 14, y + 112, selected->contents[0] ? selected->contents : "(empty file)", RGB(71, 82, 90), 1);
    }
}

static void draw_store_window(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    gfx_fill_rect(x, y, width, height, RGB(245, 248, 252));
    gfx_fill_round_rect_alpha(x + 14, y + 14, width - 28, 46, 16, theme->accent, 52);
    gfx_draw_text(x + 28, y + 25, "LiquidOS Store", theme->text, 2);

    char summary[64];
    char count[16];
    u64_to_dec(app_store_installed_count(), count, sizeof(count));
    summary[0] = 0;
    append_text(summary, sizeof(summary), count);
    append_text(summary, sizeof(summary), " installed");
    append_text(summary, sizeof(summary), " / ");
    u64_to_dec(fs_free_slots(), count, sizeof(count));
    append_text(summary, sizeof(summary), count);
    append_text(summary, sizeof(summary), " slots free");
    gfx_draw_text(x + width - 150, y + 30, summary, RGB(63, 74, 84), 1);

    i32 card_y = y + 68;
    for (size_t i = 0; i < app_store_count(); i++) {
        const StoreApp *app = app_store_get(i);
        if (!app) {
            continue;
        }

        bool installed = app_store_is_installed(i);
        i32 row_y = card_y + (i32)i * 86;
        Color card = installed ? RGB(235, 249, 241) : RGB(255, 255, 255);
        gfx_fill_round_rect_alpha(x + 16, row_y, width - 32, 72, 16, RGB(0, 0, 0), 28);
        gfx_fill_round_rect(x + 14, row_y - 2, width - 32, 72, 16, card);
        gfx_fill_round_rect_alpha(x + 28, row_y + 13, 42, 42, 14, theme->accent, 145);
        gfx_draw_text(x + 42, row_y + 24, app->name, RGB(255, 255, 255), 1);
        gfx_draw_text(x + 84, row_y + 12, app->display_name, theme->text, 1);
        gfx_draw_text(x + 84, row_y + 30, app->description, RGB(72, 82, 92), 1);
        gfx_draw_text(x + 84, row_y + 48, app->package_name, RGB(104, 114, 124), 1);

        i32 primary_x = x + width - 190;
        i32 remove_x = x + width - 108;
        gfx_fill_round_rect_alpha(primary_x, row_y + 22, 76, 30, 11, installed ? RGB(68, 160, 104) : theme->accent, 230);
        gfx_draw_text(primary_x + 18, row_y + 31, installed ? "RUN" : "INSTALL", RGB(255, 255, 255), 1);
        if (installed) {
            gfx_fill_round_rect_alpha(remove_x, row_y + 22, 78, 30, 11, RGB(180, 74, 82), 218);
            gfx_draw_text(remove_x + 13, row_y + 31, "REMOVE", RGB(255, 255, 255), 1);
        }
    }

    gfx_draw_text(x + 22, y + height - 24, app_status_text, RGB(77, 88, 98), 1);
}

static void draw_launcher_tile(i32 x, i32 y, const char *name, const char *kind, Color accent) {
    gfx_fill_round_rect_alpha(x + 3, y + 4, 112, 82, 18, RGB(0, 0, 0), 32);
    gfx_fill_round_rect(x, y, 112, 82, 18, RGB(255, 255, 255));
    gfx_fill_round_rect_alpha(x + 34, y + 12, 44, 34, 14, accent, 220);
    gfx_draw_text(x + 18, y + 54, name, RGB(35, 43, 52), 1);
    gfx_draw_text(x + 18, y + 68, kind, RGB(100, 110, 120), 1);
}

static void draw_launcher_window(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    gfx_fill_rect(x, y, width, height, RGB(246, 248, 252));
    gfx_draw_text(x + 22, y + 22, "Launch Apps", theme->text, 2);
    gfx_draw_text(x + 24, y + 52, "Built-in tools and installed Store apps.", RGB(82, 94, 104), 1);

    i32 tile_y = y + 72;
    draw_launcher_tile(x + 22, tile_y, "Liqueia", "built-in", RGB(217, 164, 75));
    draw_launcher_tile(x + 154, tile_y, "Files", "built-in", RGB(100, 172, 230));
    draw_launcher_tile(x + 286, tile_y, "Terminal", "built-in", RGB(80, 210, 150));

    i32 installed_y = tile_y + 122;
    gfx_draw_text(x + 24, installed_y - 24, "Installed apps", theme->text, 1);
    size_t visible = 0;
    for (size_t i = 0; i < app_store_count(); i++) {
        if (!app_store_is_installed(i)) {
            continue;
        }
        const StoreApp *app = app_store_get(i);
        i32 row_y = installed_y + (i32)visible * 44;
        gfx_fill_round_rect_alpha(x + 22, row_y, width - 56, 36, 12, RGB(255, 255, 255), 235);
        gfx_fill_round_rect_alpha(x + 34, row_y + 8, 20, 20, 8, theme->accent, 210);
        gfx_draw_text(x + 66, row_y + 10, app->display_name, theme->text, 1);
        gfx_draw_text(x + width - 130, row_y + 10, "RUN", RGB(72, 120, 92), 1);
        visible++;
    }
    if (visible == 0) {
        gfx_draw_text(x + 28, installed_y + 8, "Install apps from the Store to see them here.", RGB(92, 104, 116), 1);
    }
    gfx_draw_text(x + 28, y + height - 24, app_status_text, RGB(92, 104, 116), 1);
}

static void draw_app_view_window(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    const StoreApp *app = app_store_get(selected_store_app);
    gfx_fill_rect(x, y, width, height, RGB(248, 250, 253));
    if (!app) {
        gfx_draw_text(x + 22, y + 22, "No app selected", theme->text, 2);
        return;
    }

    gfx_fill_round_rect_alpha(x + 20, y + 20, 58, 58, 18, theme->accent, 220);
    gfx_draw_text(x + 96, y + 24, app->display_name, theme->text, 2);
    gfx_draw_text(x + 98, y + 56, app->description, RGB(78, 90, 102), 1);
    gfx_fill_round_rect_alpha(x + width - 166, y + 22, 62, 28, 10, RGB(68, 160, 104), 220);
    gfx_draw_text(x + width - 146, y + 31, "RUN", RGB(255, 255, 255), 1);
    gfx_fill_round_rect_alpha(x + width - 96, y + 22, 72, 28, 10, RGB(180, 74, 82), 218);
    gfx_draw_text(x + width - 84, y + 31, "REMOVE", RGB(255, 255, 255), 1);

    const FsFile *manifest = fs_find(app_store_manifest_path(selected_store_app));
    gfx_fill_round_rect_alpha(x + 22, y + 102, width - 44, height - 126, 14, RGB(255, 255, 255), 235);
    gfx_draw_text(x + 38, y + 118, "Manifest", theme->text, 1);
    if (manifest) {
        char line[96];
        size_t used = 0;
        i32 line_y = y + 142;
        const char *text = manifest->contents;
        while (*text && line_y < y + height - 58) {
            if (*text == '\n' || used + 1 >= sizeof(line)) {
                line[used] = 0;
                gfx_draw_text(x + 38, line_y, line, RGB(60, 70, 80), 1);
                line_y += 18;
                used = 0;
                if (*text == '\n') {
                    text++;
                }
                continue;
            }
            line[used++] = *text++;
        }
        if (used > 0 && line_y < y + height - 58) {
            line[used] = 0;
            gfx_draw_text(x + 38, line_y, line, RGB(60, 70, 80), 1);
        }
    } else {
        gfx_draw_text(x + 38, y + 142, "App is not installed.", RGB(120, 72, 78), 1);
    }
    gfx_draw_text(x + 38, y + height - 38, app_status_text, RGB(93, 103, 113), 1);
}

static void draw_settings_window(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    gfx_fill_rect(x, y, width, height, RGB(247, 249, 252));
    gfx_draw_text(x + 22, y + 22, "Personalize LiquidOS", theme->text, 2);
    gfx_draw_text(x + 24, y + 52, "Themes, persistence, and installed app controls.", RGB(82, 94, 104), 1);

    i32 start_y = y + 78;
    i32 theme_w = width / 2 - 38;
    gfx_draw_text(x + 20, y + 72, "Themes", theme->text, 1);
    for (size_t i = 0; i < sizeof(themes) / sizeof(themes[0]); i++) {
        const DesktopTheme *choice = &themes[i];
        i32 row_y = start_y + 18 + (i32)i * 46;
        bool selected = i == current_theme;
        gfx_fill_round_rect_alpha(x + 18, row_y, theme_w, 36, 14, selected ? choice->accent : RGB(255, 255, 255), selected ? 70 : 235);
        gfx_draw_round_rect(x + 18, row_y, theme_w, 36, 14, selected ? choice->accent : RGB(214, 222, 230));
        gfx_fill_round_rect_alpha(x + 32, row_y + 9, 24, 24, 9, choice->wash_top, 230);
        gfx_fill_round_rect_alpha(x + 50, row_y + 9, 24, 24, 9, choice->wash_bottom, 210);
        gfx_draw_text(x + 88, row_y + 11, choice->name, choice->text, 1);
        if (selected) {
            gfx_draw_text(x + theme_w - 50, row_y + 11, "ACTIVE", choice->text, 1);
        }
    }

    i32 manager_x = x + width / 2 + 6;
    i32 manager_w = width / 2 - 34;
    gfx_draw_text(manager_x, y + 72, "App Manager", theme->text, 1);
    gfx_fill_round_rect_alpha(manager_x, y + 92, manager_w, 138, 14, RGB(255, 255, 255), 220);

    size_t visible = 0;
    i32 row_y = y + 100;
    for (size_t i = 0; i < app_store_count(); i++) {
        if (!app_store_is_installed(i)) {
            continue;
        }
        const StoreApp *app = app_store_get(i);
        gfx_draw_text(manager_x + 12, row_y + 10, app->display_name, theme->text, 1);
        gfx_fill_round_rect_alpha(manager_x + manager_w - 112, row_y + 5, 44, 24, 9, RGB(68, 160, 104), 220);
        gfx_draw_text(manager_x + manager_w - 101, row_y + 12, "RUN", RGB(255, 255, 255), 1);
        gfx_fill_round_rect_alpha(manager_x + manager_w - 62, row_y + 5, 54, 24, 9, RGB(180, 74, 82), 218);
        gfx_draw_text(manager_x + manager_w - 55, row_y + 12, "DEL", RGB(255, 255, 255), 1);
        row_y += 38;
        visible++;
    }
    if (visible == 0) {
        gfx_draw_text(manager_x + 12, y + 112, "No Store apps installed yet.", RGB(92, 104, 116), 1);
    }

    char storage[64];
    char number[16];
    storage[0] = 0;
    append_text(storage, sizeof(storage), fs_persistence_available() ? "Persistence: disk-backed" : "Persistence: RAM only");
    gfx_draw_text(manager_x, y + 252, storage, RGB(82, 94, 104), 1);
    storage[0] = 0;
    append_text(storage, sizeof(storage), "LiquidFS: ");
    u64_to_dec(fs_file_count(), number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), "/");
    u64_to_dec(fs_capacity(), number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), " files used");
    gfx_draw_text(manager_x, y + 272, storage, RGB(82, 94, 104), 1);

    PlatformSummary summary = platform_summary();
    storage[0] = 0;
    append_text(storage, sizeof(storage), "Platform: ");
    u64_to_dec(summary.available, number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), " ready / ");
    u64_to_dec(summary.partial, number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), " partial");
    gfx_draw_text(manager_x, y + 292, storage, RGB(82, 94, 104), 1);
    gfx_draw_text(manager_x, y + 314, app_status_text, RGB(82, 94, 104), 1);
}

static void draw_window(WindowKind kind) {
    Window *window = &windows[kind];
    if (!window->open) {
        return;
    }

    bool focused = focused_window == kind;
    draw_window_frame(window, focused);

    i32 content_x = window->x + 6;
    i32 content_y = window->y + 30;
    i32 content_w = window->width - 12;
    i32 content_h = window->height - 36;

    if (kind == WINDOW_TERMINAL) {
        terminal_render(content_x, content_y, content_w, content_h, focused);
    } else if (kind == WINDOW_BROWSER) {
        liqueia_render(content_x, content_y, content_w, content_h);
    } else if (kind == WINDOW_FILES) {
        draw_file_explorer(content_x, content_y, content_w, content_h);
    } else if (kind == WINDOW_STORE) {
        draw_store_window(content_x, content_y, content_w, content_h);
    } else if (kind == WINDOW_SETTINGS) {
        draw_settings_window(content_x, content_y, content_w, content_h);
    } else if (kind == WINDOW_LAUNCHER) {
        draw_launcher_window(content_x, content_y, content_w, content_h);
    } else if (kind == WINDOW_APP_VIEW) {
        draw_app_view_window(content_x, content_y, content_w, content_h);
    }
}

static void draw_taskbar(void) {
    TaskbarLayout bar;
    taskbar_layout(&bar);
    i32 compact = gfx_width() < 900 ? 1 : 0;
    i32 radius = compact ? 24 : 28;
    i32 text_scale = compact ? 1 : 2;
    i32 logo_size = compact ? 42 : 58;
    i32 app_size = compact ? 48 : 56;

    gfx_fill_round_rect_plain_alpha(bar.x + 4, bar.y + 5, bar.w, bar.h, radius, RGB(0, 0, 0), 110);
    gfx_fill_round_rect_plain_alpha(bar.x, bar.y, bar.w, bar.h, radius, RGB(8, 8, 9), 252);
    gfx_fill_round_rect_plain_alpha(bar.x + 3, bar.y + 3, bar.w - 6, 2, radius / 2, RGB(37, 37, 39), 150);

    gfx_draw_argb8888_image_scaled(bar.x + (compact ? 16 : 28), bar.y + (bar.h - logo_size) / 2, logo_size, logo_size,
                                   liquidos_logo_argb, LIQUIDOS_LOGO_WIDTH, LIQUIDOS_LOGO_HEIGHT);
    gfx_draw_text(bar.x + (compact ? 68 : 94), bar.y + (compact ? 15 : 13), clock_text, RGB(221, 223, 229), text_scale);
    gfx_draw_text(bar.x + (compact ? 68 : 94), bar.y + (compact ? 37 : 45), date_text, RGB(205, 207, 214), text_scale);

    gfx_draw_argb8888_image_scaled(bar.wifi_x, bar.y + (compact ? 20 : 21), compact ? 38 : 40, compact ? 38 : 40,
                                   system_icon_wifi_argb, SYSTEM_ICON_WIFI_WIDTH, SYSTEM_ICON_WIFI_HEIGHT);
    if (!wifi_enabled) {
        gfx_draw_line(bar.wifi_x + 3, bar.y + 25, bar.wifi_x + 37, bar.y + 58, RGB(238, 122, 122));
        gfx_draw_line(bar.wifi_x + 4, bar.y + 25, bar.wifi_x + 38, bar.y + 58, RGB(238, 122, 122));
    }

    for (i32 i = 0; i < bar.slots_available; i++) {
        bool active = (i == 0 && focused_window == WINDOW_BROWSER && windows[WINDOW_BROWSER].open) ||
                      (i == 1 && focused_window == WINDOW_FILES && windows[WINDOW_FILES].open) ||
                      (i == 2 && focused_window == WINDOW_STORE && windows[WINDOW_STORE].open) ||
                      (i == 3 && focused_window == WINDOW_SETTINGS && windows[WINDOW_SETTINGS].open);
        draw_app_slot(bar.slot_x + i * bar.slot_step, bar.slot_y, bar.slot_size, active, false);
    }
    gfx_draw_argb8888_image_scaled(bar.slot_x + 1, bar.slot_y + 1, app_size, app_size,
                                   app_icon_browser_argb, APP_ICON_BROWSER_WIDTH, APP_ICON_BROWSER_HEIGHT);
    gfx_draw_argb8888_image_scaled(bar.slot_x + bar.slot_step + 1, bar.slot_y + 1, app_size, app_size,
                                   app_icon_files_argb, APP_ICON_FILES_WIDTH, APP_ICON_FILES_HEIGHT);
    i32 store_x = bar.slot_x + bar.slot_step * 2;
    i32 settings_x = bar.slot_x + bar.slot_step * 3;
    gfx_fill_round_rect_alpha(store_x + 9, bar.slot_y + 11, app_size - 16, app_size - 18, 12, themes[current_theme].accent, 235);
    gfx_draw_line(store_x + 18, bar.slot_y + 19, store_x + app_size - 16, bar.slot_y + 19, RGB(255, 255, 255));
    gfx_draw_text(store_x + 18, bar.slot_y + 30, "GET", RGB(255, 255, 255), 1);
    gfx_fill_round_rect_alpha(settings_x + 10, bar.slot_y + 10, app_size - 18, app_size - 18, 16, RGB(238, 242, 248), 238);
    gfx_fill_circle(settings_x + app_size / 2, bar.slot_y + app_size / 2, 13, themes[current_theme].accent);
    gfx_fill_circle(settings_x + app_size / 2, bar.slot_y + app_size / 2, 5, RGB(20, 24, 30));

    gfx_fill_round_rect_plain_alpha(bar.search_x + 3, bar.search_y + 4, bar.search_w, bar.search_h, bar.search_h / 2, RGB(0, 0, 0), 80);
    gfx_fill_round_rect_plain_alpha(bar.search_x, bar.search_y, bar.search_w, bar.search_h, bar.search_h / 2, RGB(56, 56, 57), 255);
    gfx_draw_argb8888_image_scaled(bar.search_x + (compact ? 15 : 16), bar.search_y + (compact ? 5 : 9), compact ? 32 : 38, compact ? 32 : 38,
                                   system_icon_search_argb, SYSTEM_ICON_SEARCH_WIDTH, SYSTEM_ICON_SEARCH_HEIGHT);
    gfx_draw_text(bar.search_x + (compact ? 54 : 58), bar.search_y + (compact ? 13 : 16), "Search", RGB(206, 207, 213), text_scale);
}

void ui_init(const BootInfo *boot) {
    (void)boot;

    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();

    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;
    previous_mouse_x = mouse_x;
    previous_mouse_y = mouse_y;

    update_clock_text();
    gfx_prepare_wallpaper_rgb565(background_image_rgb565, BACKGROUND_IMAGE_WIDTH, BACKGROUND_IMAGE_HEIGHT);
    load_theme_setting();

    windows[WINDOW_TERMINAL] = (Window){ 90, 160, 720, 410, "Terminal", false, false };
    windows[WINDOW_BROWSER] = (Window){ 180, 180, 820, 500, "Liqueia", false, false };
    windows[WINDOW_FILES] = (Window){ 320, 260, 660, 400, "Files", false, false };
    windows[WINDOW_STORE] = (Window){ 240, 170, 720, 430, "Store", false, false };
    windows[WINDOW_SETTINGS] = (Window){ 280, 210, 620, 360, "Personalize", false, false };
    windows[WINDOW_LAUNCHER] = (Window){ 260, 150, 600, 410, "Launch Apps", false, false };
    windows[WINDOW_APP_VIEW] = (Window){ 300, 190, 620, 390, "App", false, false };

    if (screen_w < 800) {
        windows[WINDOW_TERMINAL] = (Window){ 25, 100, screen_w - 50, 310, "Terminal", false, false };
        windows[WINDOW_BROWSER] = (Window){ 40, 120, screen_w - 80, 320, "Liqueia", false, false };
        windows[WINDOW_FILES] = (Window){ 55, 140, screen_w - 110, 270, "Files", false, false };
        windows[WINDOW_STORE] = (Window){ 35, 110, screen_w - 70, 330, "Store", false, false };
        windows[WINDOW_SETTINGS] = (Window){ 45, 130, screen_w - 90, 310, "Personalize", false, false };
        windows[WINDOW_LAUNCHER] = (Window){ 30, 105, screen_w - 60, 330, "Launch Apps", false, false };
        windows[WINDOW_APP_VIEW] = (Window){ 50, 125, screen_w - 100, 315, "App", false, false };
    }

    clamp_window(&windows[WINDOW_TERMINAL]);
    clamp_window(&windows[WINDOW_BROWSER]);
    clamp_window(&windows[WINDOW_FILES]);
    clamp_window(&windows[WINDOW_STORE]);
    clamp_window(&windows[WINDOW_SETTINGS]);
    clamp_window(&windows[WINDOW_LAUNCHER]);
    clamp_window(&windows[WINDOW_APP_VIEW]);

    z_order[0] = WINDOW_TERMINAL;
    z_order[1] = WINDOW_FILES;
    z_order[2] = WINDOW_STORE;
    z_order[3] = WINDOW_SETTINGS;
    z_order[4] = WINDOW_LAUNCHER;
    z_order[5] = WINDOW_APP_VIEW;
    z_order[6] = WINDOW_BROWSER;
    focused_window = WINDOW_BROWSER;
    terminal_init();
    liqueia_init();
    mark_dirty_full();
    cursor_redraw_needed = true;

    i32 new_hover_zone = taskbar_hover_zone_at(mouse_x, mouse_y);
    if (new_hover_zone != hover_zone) {
        hover_zone = new_hover_zone;
    }
}

void ui_handle_event(const InputEvent *event) {
    if (event->type == INPUT_EVENT_KEY) {
        if (focused_window == WINDOW_TERMINAL && windows[WINDOW_TERMINAL].open) {
            terminal_on_char(event->ch);
            mark_dirty_window(WINDOW_TERMINAL);
        } else if (focused_window == WINDOW_BROWSER && windows[WINDOW_BROWSER].open) {
            liqueia_on_char(event->ch);
            mark_dirty_window(WINDOW_BROWSER);
        }
        return;
    }

    if (event->type != INPUT_EVENT_MOUSE) {
        return;
    }

    bool mouse_moved = event->dx != 0 || event->dy != 0;
    mouse_x += event->dx;
    mouse_y += event->dy;

    i32 max_x = (i32)gfx_width() - 1;
    i32 max_y = (i32)gfx_height() - 1;

    if (mouse_x < 0) {
        mouse_x = 0;
    }
    if (mouse_y < 0) {
        mouse_y = 0;
    }
    if (mouse_x > max_x) {
        mouse_x = max_x;
    }
    if (mouse_y > max_y) {
        mouse_y = max_y;
    }

    if (mouse_moved) {
        cursor_redraw_needed = true;
    }

    i32 new_hover_zone = taskbar_hover_zone_at(mouse_x, mouse_y);
    if (new_hover_zone != hover_zone) {
        hover_zone = new_hover_zone;
    }

    bool left_now = event->left_down;

    if (dragging && left_now && mouse_moved) {
        Window *window = &windows[dragged_window];
        mark_dirty_rect(window->x, window->y, window->width, window->height);
        window->x = mouse_x - drag_offset_x;
        window->y = mouse_y - drag_offset_y;
        clamp_window(window);
        mark_dirty_rect(window->x, window->y, window->width, window->height);
    }

    if (resizing && left_now && mouse_moved) {
        Window *window = &windows[resized_window];
        mark_dirty_rect(window->x, window->y, window->width, window->height);
        window->width = mouse_x - window->x + 8;
        window->height = mouse_y - window->y + 8;
        window->expanded = false;
        clamp_window(window);
        mark_dirty_rect(window->x, window->y, window->width, window->height);
    }

    if (left_now && !previous_left) {
        if (point_in_rect(mouse_x, mouse_y, taskbar_x(), taskbar_y(), taskbar_w(), taskbar_h())) {
            handle_taskbar_click();
            mark_dirty_taskbar();
        } else {
            handle_window_click();
        }
    }

    if (!left_now) {
        dragging = false;
        resizing = false;
    }

    previous_left = left_now;
}

void ui_update(u64 tick_count) {
    if (tick_count - last_clock_tick >= 100) {
        char old_clock[6];
        char old_date[9];
        strcpy(old_clock, clock_text);
        strcpy(old_date, date_text);
        update_clock_text();
        last_clock_tick = tick_count;
        if (strcmp(old_clock, clock_text) != 0 || strcmp(old_date, date_text) != 0) {
            mark_dirty_taskbar();
        }
    }
}

void ui_render(void) {
    if (full_redraw_needed) {
        if (!dirty_region_valid) {
            mark_dirty_full();
        }

        i32 present_x = dirty_x0;
        i32 present_y = dirty_y0;
        i32 present_w = dirty_x1 - dirty_x0;
        i32 present_h = dirty_y1 - dirty_y0;

        gfx_set_clip(present_x, present_y, present_w, present_h);
        draw_background();

        for (u32 i = 0; i < WINDOW_COUNT; i++) {
            draw_window(z_order[i]);
        }

        draw_taskbar();
        gfx_clear_clip();
        gfx_present_rect(present_x, present_y, present_w, present_h);
        gfx_present_cursor(mouse_x, mouse_y, mouse_x, mouse_y);
        previous_mouse_x = mouse_x;
        previous_mouse_y = mouse_y;
        full_redraw_needed = false;
        dirty_region_valid = false;
        cursor_redraw_needed = false;
        return;
    }

    if (cursor_redraw_needed) {
        gfx_present_cursor(previous_mouse_x, previous_mouse_y, mouse_x, mouse_y);
        previous_mouse_x = mouse_x;
        previous_mouse_y = mouse_y;
        cursor_redraw_needed = false;
    }
}
