#include <liquidos/fs.h>
#include <liquidos/app_store.h>
#include <liquidos/gfx.h>
#include <liquidos/input.h>
#include <liquidos/io.h>
#include <liquidos/lib.h>
#include <liquidos/liqueia.h>
#include <liquidos/platform.h>
#include <liquidos/power.h>
#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/terminal.h>
#include <liquidos/ui.h>
#include "../gfx/app_icons.h"
#include "../gfx/background_image.h"

typedef enum WindowKind {
    WINDOW_TERMINAL = 0,
    WINDOW_BROWSER = 1,
    WINDOW_FILES = 2,
    WINDOW_STORE = 3,
    WINDOW_SETTINGS = 4,
    WINDOW_LAUNCHER = 5,
    WINDOW_APP_VIEW = 6,
    WINDOW_CONTROL_CENTER = 7,
    WINDOW_COUNT = 8
} WindowKind;

typedef struct Window {
    i32 x;
    i32 y;
    i32 width;
    i32 height;
    i32 restore_x;
    i32 restore_y;
    i32 restore_width;
    i32 restore_height;
    const char *title;
    bool open;
    bool minimized;
    bool expanded;
} Window;

typedef struct Notification {
    char title[24];
    char body[64];
    u64 created_tick;
    bool unread;
} Notification;

typedef struct TaskbarLayout {
    i32 x;
    i32 y;
    i32 w;
    i32 h;
    i32 volume_x;
    i32 wifi_x;
    i32 control_x;
    i32 logo_x;
    i32 slot_x;
    i32 slot_y;
    i32 slot_w;
    i32 slot_h;
    i32 slot_step;
    i32 slots_available;
    i32 clock_x;
    i32 clock_y;
    i32 clock_w;
    i32 clock_h;
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

typedef struct DockApp {
    WindowKind window;
    const char *message;
    const u32 *pixels;
    u32 icon_width;
    u32 icon_height;
    Color tile_top;
    Color tile_bottom;
    bool small_artwork;
} DockApp;

static const DesktopTheme themes[] = {
    { "Liquid Gold", RGB(255, 208, 96), RGB(24, 18, 34), RGB(244, 184, 64), RGB(248, 245, 235), RGB(31, 34, 42) },
    { "Aurora Blue", RGB(70, 198, 255), RGB(13, 25, 59), RGB(104, 207, 255), RGB(236, 246, 255), RGB(24, 36, 56) },
    { "Glass Mint", RGB(126, 244, 194), RGB(14, 44, 50), RGB(87, 220, 170), RGB(239, 252, 248), RGB(23, 50, 47) },
    { "Night Violet", RGB(180, 116, 255), RGB(20, 16, 42), RGB(190, 136, 255), RGB(245, 240, 255), RGB(38, 28, 60) },
};

static const DockApp dock_apps[] = {
    { WINDOW_BROWSER, "BROWSER", app_icon_browser_argb, APP_ICON_BROWSER_WIDTH, APP_ICON_BROWSER_HEIGHT, RGB(70, 52, 20), RGB(2, 2, 8), true },
    { WINDOW_FILES, "FILES", app_icon_files_argb, APP_ICON_FILES_WIDTH, APP_ICON_FILES_HEIGHT, RGB(238, 145, 255), RGB(116, 62, 218), true },
    { WINDOW_TERMINAL, "TERMINAL", app_icon_terminal_argb, APP_ICON_TERMINAL_WIDTH, APP_ICON_TERMINAL_HEIGHT, RGB(38, 42, 75), RGB(8, 9, 22), true },
    { WINDOW_SETTINGS, "SETTINGS", app_icon_settings_argb, APP_ICON_SETTINGS_WIDTH, APP_ICON_SETTINGS_HEIGHT, RGB(104, 130, 164), RGB(54, 61, 78), false },
};

static Window windows[WINDOW_COUNT];
static WindowKind z_order[WINDOW_COUNT];
static WindowKind focused_window = WINDOW_TERMINAL;
static i32 mouse_x = 320;
static i32 mouse_y = 240;
static bool previous_left = false;
static bool previous_right = false;
static bool dragging = false;
static bool resizing = false;
static bool dock_resizing = false;
static bool dock_grip_visible = false;
static WindowKind dragged_window = WINDOW_TERMINAL;
static WindowKind resized_window = WINDOW_TERMINAL;
static i32 drag_offset_x = 0;
static i32 drag_offset_y = 0;
static i32 dock_resize_start_x = 0;
static i32 dock_resize_start_scale = 100;
static i32 dock_scale_percent = 100;
static u64 last_clock_tick = 0;
static char clock_text[6] = "00:00";
static char date_text[11] = "00/00/0000";
static char taskbar_message[32] = "SEARCH";
static char app_status_text[64] = "Install an app, then run it from Store or Launch Apps.";
static char file_clipboard[FS_NAME_LENGTH] = "";
static bool wifi_enabled = true;
static bool battery_saver = false;
static bool full_redraw_needed = true;
static bool cursor_redraw_needed = true;
static bool dirty_region_valid = false;
static bool context_menu_open = false;
static i32 context_menu_x = 0;
static i32 context_menu_y = 0;
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
static Notification notifications[8];
static size_t notification_head = 0;
static size_t notification_count = 0;

static void append_text(char *dest, size_t dest_size, const char *src);
static i32 taskbar_w(void);
static i32 taskbar_h(void);
static void window_title_group_layout(const Window *window, i32 *title_x, i32 *title_w, i32 *min_x, i32 *max_x, i32 *close_x, i32 *y, i32 *control_size);

static i32 dock_app_count(void) {
    return (i32)(sizeof(dock_apps) / sizeof(dock_apps[0]));
}

static i32 dock_scale(void) {
    if (dock_scale_percent < 82 || dock_scale_percent > 135) {
        dock_scale_percent = 100;
    }
    return dock_scale_percent;
}

static i32 dock_scale_value(i32 value) {
    return (value * dock_scale() + 50) / 100;
}

static i32 dock_tile_size(void) {
    return dock_scale_value(gfx_width() < 900 ? 52 : 59);
}

static i32 dock_gap(void) {
    return dock_scale_value(gfx_width() < 900 ? 13 : 14);
}

static i32 dock_clock_w(void) {
    return dock_scale_value(92);
}

static i32 taskbar_x(void) {
    i32 screen_w = (i32)gfx_width();
    i32 width = taskbar_w();
    return (screen_w - width) / 2;
}

static i32 taskbar_y(void) {
    return gfx_width() < 900 ? 14 : 18;
}

static i32 taskbar_w(void) {
    i32 screen_w = (i32)gfx_width();
    i32 count = dock_app_count();
    i32 tile_size = dock_tile_size();
    i32 gap = dock_gap();
    i32 width = gap * (count + 2) + dock_clock_w() + tile_size * count;
    i32 max_width = screen_w - 32;
    return width > max_width ? max_width : width;
}

static i32 taskbar_h(void) {
    return dock_tile_size() + dock_gap() * 2;
}

static void dock_grip_rect(const TaskbarLayout *bar, i32 *x, i32 *y, i32 *size) {
    i32 grip = dock_scale_value(16);
    *x = bar->x + bar->w - grip - dock_scale_value(5);
    *y = bar->y + bar->h - grip - dock_scale_value(5);
    *size = grip;
}

static void taskbar_layout(TaskbarLayout *layout) {
    i32 gap = dock_gap();

    layout->x = taskbar_x();
    layout->y = taskbar_y();
    layout->w = taskbar_w();
    layout->h = taskbar_h();

    layout->slot_w = dock_tile_size();
    layout->slot_h = dock_tile_size();
    layout->slot_step = layout->slot_w + gap;
    layout->slots_available = dock_app_count();
    layout->clock_x = layout->x + gap;
    layout->clock_y = layout->y + (layout->h - layout->slot_h) / 2;
    layout->clock_w = dock_clock_w();
    layout->clock_h = layout->slot_h;
    layout->search_w = 0;
    layout->search_h = 0;
    layout->search_y = 0;
    layout->control_x = 0;
    layout->volume_x = 0;
    layout->wifi_x = 0;
    layout->logo_x = 0;
    layout->slot_y = layout->y + (layout->h - layout->slot_h) / 2;
    layout->slot_x = layout->clock_x + layout->clock_w + gap;
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

static Window make_window(i32 x, i32 y, i32 width, i32 height, const char *title) {
    Window window;
    window.x = x;
    window.y = y;
    window.width = width;
    window.height = height;
    window.restore_x = x;
    window.restore_y = y;
    window.restore_width = width;
    window.restore_height = height;
    window.title = title;
    window.open = false;
    window.minimized = false;
    window.expanded = false;
    return window;
}

static void remember_window_restore(Window *window) {
    if (!window->expanded) {
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_width = window->width;
        window->restore_height = window->height;
    }
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
    windows[kind].minimized = false;
    bring_to_front(kind);
    mark_dirty_window(kind);
}

static void push_notification(const char *title, const char *body) {
    Notification *note = &notifications[notification_head];
    strncpy(note->title, title, sizeof(note->title) - 1);
    note->title[sizeof(note->title) - 1] = 0;
    strncpy(note->body, body, sizeof(note->body) - 1);
    note->body[sizeof(note->body) - 1] = 0;
    note->created_tick = scheduler_ticks();
    note->unread = true;

    notification_head = (notification_head + 1) % (sizeof(notifications) / sizeof(notifications[0]));
    if (notification_count < sizeof(notifications) / sizeof(notifications[0])) {
        notification_count++;
    }
    mark_dirty_full();
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
    i32 grip_x;
    i32 grip_y;
    i32 grip_size;
    dock_grip_rect(&bar, &grip_x, &grip_y, &grip_size);

    if (!point_in_rect(px, py, bar.x, bar.y, bar.w, bar.h)) {
        return -1;
    }
    if (point_in_rect(px, py, grip_x, grip_y, grip_size, grip_size)) {
        return 2;
    }
    for (i32 i = 0; i < bar.slots_available; i++) {
        if (point_in_rect(px, py, bar.slot_x + i * bar.slot_step, bar.slot_y, bar.slot_w, bar.slot_h)) {
            return 10 + i;
        }
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
        remember_window_restore(window);
        window->x = 90;
        window->y = taskbar_y() + taskbar_h() + 28;
        window->width = (i32)gfx_width() - 180;
        window->height = (i32)gfx_height() - window->y - 70;
        window->expanded = true;
    } else {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->width = window->restore_width;
        window->height = window->restore_height;
        window->expanded = false;
    }
    clamp_window(window);
    mark_dirty_rect(window->x, window->y, window->width, window->height);
}

static void minimize_window(Window *window) {
    mark_dirty_rect(window->x, window->y, window->width, window->height);
    window->minimized = true;
    mark_dirty_taskbar();
}

static void handle_taskbar_click(void) {
    TaskbarLayout bar;
    taskbar_layout(&bar);
    i32 grip_x;
    i32 grip_y;
    i32 grip_size;
    dock_grip_rect(&bar, &grip_x, &grip_y, &grip_size);

    dock_grip_visible = true;
    if (point_in_rect(mouse_x, mouse_y, grip_x - 4, grip_y - 4, grip_size + 8, grip_size + 8)) {
        dock_resizing = true;
        dock_resize_start_x = mouse_x;
        dock_resize_start_scale = dock_scale_percent;
        mark_dirty_taskbar();
        return;
    }

    for (i32 i = 0; i < bar.slots_available; i++) {
        if (point_in_rect(mouse_x, mouse_y, bar.slot_x + i * bar.slot_step, bar.slot_y, bar.slot_w, bar.slot_h)) {
            open_window(dock_apps[i].window);
            set_taskbar_message(dock_apps[i].message);
            return;
        }
    }
}

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
}

static const char *process_state_name(ProcessState state) {
    switch (state) {
    case PROCESS_READY: return "ready";
    case PROCESS_RUNNING: return "running";
    case PROCESS_SLEEPING: return "sleep";
    case PROCESS_STOPPED: return "stopped";
    case PROCESS_CRASHED: return "crashed";
    default: return "unused";
    }
}

static void make_untitled_name(char *out, size_t out_size) {
    char number[16];
    u64_to_dec(new_file_counter++, number, sizeof(number));
    out[0] = 0;
    append_text(out, out_size, "DESKTOP/UNTITLED");
    append_text(out, out_size, number);
    append_text(out, out_size, ".TXT");
}

static void make_file_variant_name(const char *source, const char *prefix, const char *suffix, char *out, size_t out_size) {
    const char *base = source;
    const char *slash = source;
    while (*slash) {
        if (*slash == '/') {
            base = slash + 1;
        }
        slash++;
    }

    out[0] = 0;
    append_text(out, out_size, prefix);
    append_text(out, out_size, base);
    append_text(out, out_size, suffix);
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
            push_notification("Files", "Created a new file.");
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
            push_notification("Files", "Deleted selected file.");
            mark_dirty_rect(window->x, window->y, window->width, window->height);
        }
        return;
    }

    if (point_in_rect(mouse_x, mouse_y, x + 188, y + 10, 74, 28)) {
        const FsFile *file = fs_get_file((size_t)selected_file_index);
        char copy_name[FS_NAME_LENGTH];
        if (file) {
            make_file_variant_name(file->name, "DESKTOP/COPY-", "", copy_name, sizeof(copy_name));
            if (fs_copy(file->name, copy_name)) {
                selected_file_index = fs_find_index(copy_name);
                strncpy(file_clipboard, copy_name, sizeof(file_clipboard) - 1);
                file_clipboard[sizeof(file_clipboard) - 1] = 0;
                set_taskbar_message("FILE COPIED");
                push_notification("Files", "Copied selected file.");
            } else {
                set_taskbar_message("COPY FAILED");
            }
            mark_dirty_rect(window->x, window->y, window->width, window->height);
        }
        return;
    }

    if (point_in_rect(mouse_x, mouse_y, x + 270, y + 10, 86, 28)) {
        const FsFile *file = fs_get_file((size_t)selected_file_index);
        char new_name[FS_NAME_LENGTH];
        if (file) {
            make_file_variant_name(file->name, "DESKTOP/RENAMED-", "", new_name, sizeof(new_name));
            if (fs_rename(file->name, new_name)) {
                selected_file_index = fs_find_index(new_name);
                set_taskbar_message("FILE RENAMED");
                push_notification("Files", "Renamed selected file.");
            } else {
                set_taskbar_message("RENAME FAILED");
            }
            mark_dirty_rect(window->x, window->y, window->width, window->height);
        }
        return;
    }

    if (point_in_rect(mouse_x, mouse_y, x + 364, y + 10, 72, 28)) {
        const FsFile *file = fs_get_file((size_t)selected_file_index);
        char moved_name[FS_NAME_LENGTH];
        if (file) {
            make_file_variant_name(file->name, "DOCUMENTS/", "", moved_name, sizeof(moved_name));
            if (fs_rename(file->name, moved_name)) {
                selected_file_index = fs_find_index(moved_name);
                set_taskbar_message("FILE MOVED");
                push_notification("Files", "Moved selected file.");
            } else {
                set_taskbar_message("MOVE FAILED");
            }
            mark_dirty_rect(window->x, window->y, window->width, window->height);
        }
        return;
    }

    if (point_in_rect(mouse_x, mouse_y, x + 444, y + 10, 112, 28)) {
        fs_write("DESKTOP/DEMO.TXT", "Saved from the graphical Files app. This file lives in the LiquidOS RAM filesystem.");
        selected_file_index = fs_find_index("DESKTOP/DEMO.TXT");
        set_taskbar_message("FILE SAVED");
        push_notification("Files", "Saved DESKTOP/DEMO.TXT.");
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
                push_notification("Store", message);
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
                push_notification("Store", "App removed.");
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
                push_notification("Settings", "App removed from App Manager.");
            } else {
                set_app_status("Remove failed.");
                set_taskbar_message("REMOVE FAILED");
            }
            mark_dirty_full();
            return;
        }
        row_y += 38;
    }

    i32 task_y = y + 246;
    for (size_t i = 0; i < process_count() && i < 3; i++) {
        const Process *process = process_get(i);
        if (!process) {
            continue;
        }
        i32 process_y = task_y + 28 + (i32)i * 28;
        if (process->mode == PROCESS_USER &&
            process->state != PROCESS_STOPPED &&
            process->state != PROCESS_CRASHED &&
            point_in_rect(mouse_x, mouse_y, manager_x + manager_w - 58, process_y + 2, 46, 20)) {
            if (process_kill(process->pid, -9)) {
                set_taskbar_message("APP CLOSED");
                push_notification("Task Manager", "Force closed user process.");
            } else {
                set_taskbar_message("KILL FAILED");
            }
            mark_dirty_full();
            return;
        }
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

static void handle_control_center_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;

    if (point_in_rect(mouse_x, mouse_y, x + 22, y + 64, 132, 42)) {
        wifi_enabled = !wifi_enabled;
        set_taskbar_message(wifi_enabled ? "WIFI ENABLED" : "WIFI DISABLED");
        push_notification("Control Center", wifi_enabled ? "Wi-Fi enabled." : "Wi-Fi disabled.");
        mark_dirty_full();
        return;
    }
    if (point_in_rect(mouse_x, mouse_y, x + 166, y + 64, 132, 42)) {
        battery_saver = !battery_saver;
        set_taskbar_message(battery_saver ? "BATTERY SAVER" : "FULL POWER");
        push_notification("Control Center", battery_saver ? "Battery saver enabled." : "Full power mode enabled.");
        mark_dirty_full();
        return;
    }
    if (point_in_rect(mouse_x, mouse_y, x + 22, y + window->height - 84, 112, 34)) {
        power_reboot();
    }
    if (point_in_rect(mouse_x, mouse_y, x + 146, y + window->height - 84, 132, 34)) {
        power_shutdown();
    }
}

static bool point_in_open_window(i32 x, i32 y) {
    for (int z = WINDOW_COUNT - 1; z >= 0; z--) {
        Window *window = &windows[z_order[z]];
        if (window->open && !window->minimized && point_in_rect(x, y, window->x, window->y, window->width, window->height)) {
            return true;
        }
    }
    return false;
}

static void handle_desktop_click(void) {
    if (!context_menu_open) {
        return;
    }

    i32 row = (mouse_y - context_menu_y) / 28;
    if (point_in_rect(mouse_x, mouse_y, context_menu_x, context_menu_y, 156, 116)) {
        if (row == 0) {
            open_window(WINDOW_LAUNCHER);
        } else if (row == 1) {
            open_window(WINDOW_FILES);
        } else if (row == 2) {
            open_window(WINDOW_SETTINGS);
        } else if (row == 3) {
            char name[FS_NAME_LENGTH];
            make_untitled_name(name, sizeof(name));
            if (fs_write(name, "Created from the LiquidOS desktop context menu.")) {
                selected_file_index = fs_find_index(name);
                set_taskbar_message("FILE CREATED");
                push_notification("Desktop", "Created a new desktop file.");
            }
        }
    }
    context_menu_open = false;
    mark_dirty_full();
}

static void handle_window_click(void) {
    for (int z = WINDOW_COUNT - 1; z >= 0; z--) {
        WindowKind kind = z_order[z];
        Window *window = &windows[kind];

        if (!window->open || window->minimized) {
            continue;
        }

        if (!point_in_rect(mouse_x, mouse_y, window->x, window->y, window->width, window->height)) {
            continue;
        }

        bring_to_front(kind);

        i32 title_x;
        i32 title_w;
        i32 min_x;
        i32 max_x;
        i32 close_x;
        i32 control_y;
        i32 control_size;
        window_title_group_layout(window, &title_x, &title_w, &min_x, &max_x, &close_x, &control_y, &control_size);

        if (point_in_rect(mouse_x, mouse_y, min_x, control_y, control_size, control_size)) {
            minimize_window(window);
            return;
        }

        if (point_in_rect(mouse_x, mouse_y, max_x, control_y, control_size, control_size)) {
            toggle_window_size(window);
            return;
        }

        if (point_in_rect(mouse_x, mouse_y, close_x, control_y, control_size, control_size)) {
            mark_dirty_rect(window->x, window->y, window->width, window->height);
            window->open = false;
            window->minimized = false;
            push_notification("Window closed", window->title);
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
            } else if (kind == WINDOW_CONTROL_CENTER) {
                handle_control_center_click(window);
            }
        }

        return;
    }
}

static u8 bcd_to_binary(u8 value) {
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

static bool is_leap_year(u16 year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static u8 days_in_month(u16 year, u8 month) {
    static const u8 days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 31;
    }
    return days[month - 1];
}

static u8 day_of_week(u16 year, u8 month, u8 day) {
    static const u8 offsets[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    u16 y = year;
    if (month < 3) {
        y--;
    }
    return (u8)((y + y / 4 - y / 100 + y / 400 + offsets[month - 1] + day) % 7);
}

static u8 last_sunday_of_month(u16 year, u8 month) {
    u8 day = days_in_month(year, month);
    while (day > 1 && day_of_week(year, month, day) != 0) {
        day--;
    }
    return day;
}

static bool uk_dst_active_utc(u16 year, u8 month, u8 day, u8 hour) {
    u8 dst_start_day = last_sunday_of_month(year, 3);
    u8 dst_end_day = last_sunday_of_month(year, 10);

    if (month > 3 && month < 10) {
        return true;
    }
    if (month < 3 || month > 10) {
        return false;
    }
    if (month == 3) {
        return day > dst_start_day || (day == dst_start_day && hour >= 1);
    }
    return day < dst_end_day || (day == dst_end_day && hour < 1);
}

static void add_hours_to_date_time(u16 *year, u8 *month, u8 *day, u8 *hour, u8 hours) {
    while (hours-- > 0) {
        (*hour)++;
        if (*hour < 24) {
            continue;
        }

        *hour = 0;
        (*day)++;
        if (*day <= days_in_month(*year, *month)) {
            continue;
        }

        *day = 1;
        (*month)++;
        if (*month <= 12) {
            continue;
        }

        *month = 1;
        (*year)++;
    }
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
    u16 full_year = 2000;

    if ((status_b & 0x04) == 0) {
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }

    hour %= 24;
    minute %= 60;
    full_year = (u16)(2000 + (year % 100));
    day = day == 0 ? 1 : day;
    month = month == 0 ? 1 : month;

    if (uk_dst_active_utc(full_year, month, day, hour)) {
        add_hours_to_date_time(&full_year, &month, &day, &hour, 1);
    }

    clock_text[0] = (char)('0' + hour / 10);
    clock_text[1] = (char)('0' + hour % 10);
    clock_text[2] = ':';
    clock_text[3] = (char)('0' + minute / 10);
    clock_text[4] = (char)('0' + minute % 10);
    clock_text[5] = 0;

    date_text[0] = (char)('0' + (day % 100) / 10);
    date_text[1] = (char)('0' + day % 10);
    date_text[2] = '/';
    date_text[3] = (char)('0' + (month % 100) / 10);
    date_text[4] = (char)('0' + month % 10);
    date_text[5] = '/';
    date_text[6] = (char)('0' + (full_year / 1000) % 10);
    date_text[7] = (char)('0' + (full_year / 100) % 10);
    date_text[8] = (char)('0' + (full_year / 10) % 10);
    date_text[9] = (char)('0' + full_year % 10);
    date_text[10] = 0;
}

static void draw_glass_panel(i32 x, i32 y, i32 width, i32 height, i32 radius) {
    gfx_fill_round_rect_alpha(x + 5, y + 7, width, height, radius, RGB(0, 0, 0), 62);
    gfx_liquid_glass_rect(x, y, width, height, radius);
}

static void draw_glass_chip(i32 x, i32 y, i32 width, i32 height, const char *label, bool active) {
    gfx_fill_round_rect_alpha(x + 2, y + 3, width, height, height / 2, RGB(0, 0, 0), 34);
    gfx_liquid_glass_rect(x, y, width, height, height / 2);
    if (active) {
        gfx_fill_round_rect_alpha(x + 3, y + 3, width - 6, height - 6, height / 2, themes[current_theme].accent, 64);
    }
    gfx_draw_text(x + 12, y + (height / 2) - 5, label, active ? RGB(255, 255, 255) : RGB(34, 45, 56), 1);
}

static void draw_background(void) {
    gfx_draw_wallpaper();
}

static void draw_dock_glass_capsule(i32 x, i32 y, i32 width, i32 height, i32 radius) {
    gfx_blur_round_rect(x, y, width, height, radius);
    gfx_blur_round_rect(x + 2, y + 2, width - 4, height - 4, radius - 2);
    gfx_refract_round_rect_edges(x, y, width, height, radius, 2);
    gfx_fill_round_rect_plain_alpha(x + 2, y + 2, width - 4, height - 4, radius - 2, RGB(210, 232, 255), 8);
    gfx_draw_round_rect_alpha(x, y, width, height, radius, RGB(246, 252, 255), 74);
}

static i32 dock_child_radius(i32 child_size, i32 dock_height, i32 dock_radius) {
    if (dock_height <= 0) {
        return child_size > 0 ? child_size / 4 : 1;
    }
    return (child_size * dock_radius) / dock_height;
}

static i32 window_control_size(void) {
    return 23;
}

static i32 window_control_gap(void) {
    return 7;
}

static i32 window_title_chip_width(const Window *window) {
    i32 width = 78 + (i32)strlen(window->title) * 6;
    if (width > window->width - 130) {
        width = window->width - 130;
    }
    return width < 88 ? 88 : width;
}

static void window_title_group_layout(const Window *window, i32 *title_x, i32 *title_w, i32 *min_x, i32 *max_x, i32 *close_x, i32 *y, i32 *control_size) {
    i32 size = window_control_size();
    i32 gap = window_control_gap();
    i32 chip_w = window_title_chip_width(window);
    i32 group_w = chip_w + gap + size * 3 + gap * 2;
    i32 start_x = window->x + window->width - group_w - 12;

    if (start_x < window->x + 12) {
        start_x = window->x + 12;
    }

    *title_x = start_x;
    *title_w = chip_w;
    *min_x = start_x + chip_w + gap;
    *max_x = *min_x + size + gap;
    *close_x = *max_x + size + gap;
    *y = window->y + 6;
    *control_size = size;
}

static u8 color_channel_lerp(u8 a, u8 b, i32 t) {
    return (u8)((a * (255 - t) + b * t) / 255);
}

static Color color_lerp(Color a, Color b, i32 t) {
    return RGB(color_channel_lerp((u8)(a >> 16), (u8)(b >> 16), t),
               color_channel_lerp((u8)(a >> 8), (u8)(b >> 8), t),
               color_channel_lerp((u8)a, (u8)b, t));
}

static i32 int_sqrt(i32 value) {
    i32 root = 0;
    while ((root + 1) * (root + 1) <= value) {
        root++;
    }
    return root;
}

static i32 rounded_gradient_inset(i32 row, i32 height, i32 radius) {
    if (radius <= 0) {
        return 0;
    }

    i32 dy = 0;
    if (row < radius) {
        dy = radius - row - 1;
    } else if (row >= height - radius) {
        dy = row - (height - radius);
    } else {
        return 0;
    }

    i32 span = radius * radius - dy * dy;
    i32 inset = radius - int_sqrt(span > 0 ? span : 0);
    return inset < 0 ? 0 : inset;
}

static void draw_flat_icon_tile(i32 x, i32 y, i32 size, i32 radius, Color top, Color bottom) {
    for (i32 row = 0; row < size; row++) {
        i32 t = size > 1 ? (row * 255) / (size - 1) : 0;
        i32 inset = rounded_gradient_inset(row, size, radius);
        gfx_fill_rect(x + inset, y + row, size - inset * 2, 1, color_lerp(top, bottom, t));
    }
    gfx_draw_round_rect_alpha(x, y, size, size, radius, RGB(247, 252, 255), 35);
}

static void draw_dock_icon_asset(i32 x, i32 y, i32 tile_size, i32 radius, i32 image_size,
                                 const u32 *pixels, u32 src_w, u32 src_h, Color top, Color bottom,
                                 bool image_is_tile) {
    i32 image_x = x + (tile_size - image_size) / 2;
    i32 image_y = y + (tile_size - image_size) / 2;
    gfx_blur_round_rect(x, y, tile_size, tile_size, radius);
    gfx_refract_round_rect_edges(x, y, tile_size, tile_size, radius, 1);
    if (image_is_tile) {
        gfx_draw_argb8888_image_scaled_round(x, y, tile_size, tile_size, radius, pixels, src_w, src_h);
    } else {
        draw_flat_icon_tile(x, y, tile_size, radius, top, bottom);
        gfx_draw_argb8888_image_scaled(image_x, image_y, image_size, image_size, pixels, src_w, src_h);
    }
    gfx_draw_round_rect_alpha(x, y, tile_size, tile_size, radius, RGB(247, 252, 255), 43);
}

static void draw_dock_clock(const TaskbarLayout *bar) {
    i32 text_x = bar->clock_x + dock_scale_value(8);
    i32 text_y = bar->clock_y + (bar->clock_h - dock_scale_value(49)) / 2;
    i32 scale = dock_scale();
    u32 time_scale = (u32)scale;
    u32 date_scale = (u32)((scale * 50 + 50) / 100);
    gfx_draw_text_percent(text_x, text_y, clock_text, RGB(248, 251, 255), time_scale);
    gfx_draw_text_percent(text_x, text_y + dock_scale_value(34), date_text, RGB(248, 251, 255), date_scale);
}

static void draw_dock_divider(const TaskbarLayout *bar) {
    i32 x = bar->clock_x + bar->clock_w + dock_gap() / 2;
    i32 y = bar->y + dock_gap();
    i32 height = bar->h - dock_gap() * 2;
    gfx_fill_rect(x, y, 1, height, RGB(133, 142, 163));
    gfx_fill_rect(x + 1, y, 1, height, RGB(30, 35, 51));
}

static void draw_dock_resize_grip(const TaskbarLayout *bar) {
    i32 grip_x;
    i32 grip_y;
    i32 grip_size;
    dock_grip_rect(bar, &grip_x, &grip_y, &grip_size);
    i32 compact = gfx_width() < 900 ? 1 : 0;
    i32 dock_radius = dock_scale_value(compact ? 27 : 32);
    i32 grip_w = grip_size + dock_scale_value(9);
    i32 grip_h = grip_size;
    i32 grip_radius = dock_child_radius(grip_h, bar->h, dock_radius);
    i32 draw_x = bar->x + bar->w - grip_w - dock_scale_value(2);
    i32 draw_y = bar->y + bar->h - grip_h - dock_scale_value(2);
    (void)grip_x;
    (void)grip_y;
    gfx_liquid_glass_grip(draw_x, draw_y, grip_w, grip_h, grip_radius);
}

static void draw_window_frame(const Window *window, bool focused) {
    const DesktopTheme *theme = &themes[current_theme];
    i32 radius = 24;
    Color rim = focused ? RGB(246, 252, 255) : RGB(214, 224, 238);
    i32 title_x;
    i32 title_w;
    i32 min_x;
    i32 max_x;
    i32 close_x;
    i32 control_y;
    i32 control_size;
    window_title_group_layout(window, &title_x, &title_w, &min_x, &max_x, &close_x, &control_y, &control_size);
    i32 chip_radius = (control_size * radius) / window->height;
    if (chip_radius < 8) {
        chip_radius = 8;
    }

    gfx_blur_round_rect(window->x, window->y, window->width, window->height, radius);
    gfx_blur_round_rect(window->x + 2, window->y + 2, window->width - 4, window->height - 4, radius - 2);
    gfx_refract_round_rect_edges(window->x, window->y, window->width, window->height, radius, 2);
    gfx_fill_round_rect_plain_alpha(window->x + 2, window->y + 2, window->width - 4, window->height - 4,
                                    radius - 2, RGB(210, 232, 255), focused ? 10 : 7);
    gfx_fill_round_rect_alpha(window->x + 6, window->y + 30, window->width - 12, window->height - 36,
                              radius - 8, theme->panel, focused ? 220 : 196);
    gfx_draw_round_rect_alpha(window->x, window->y, window->width, window->height, radius,
                              rim, focused ? 76 : 48);

    gfx_blur_round_rect(title_x, control_y, title_w, control_size, chip_radius);
    gfx_refract_round_rect_edges(title_x, control_y, title_w, control_size, chip_radius, 1);
    gfx_fill_round_rect_plain_alpha(title_x, control_y, title_w, control_size, chip_radius, RGB(230, 244, 255), focused ? 24 : 14);
    gfx_draw_round_rect_alpha(title_x, control_y, title_w, control_size, chip_radius, RGB(246, 252, 255), focused ? 64 : 38);
    gfx_fill_round_rect_alpha(title_x + 7, control_y + 6, 12, 12, 5, theme->accent, focused ? 156 : 92);
    gfx_draw_text(title_x + 26, control_y + 7, window->title, focused ? theme->text : RGB(132, 142, 154), 1);

    gfx_blur_round_rect(min_x, control_y, control_size, control_size, chip_radius);
    gfx_refract_round_rect_edges(min_x, control_y, control_size, control_size, chip_radius, 1);
    gfx_fill_round_rect_plain_alpha(min_x, control_y, control_size, control_size, chip_radius, RGB(242, 190, 76), focused ? 34 : 18);
    gfx_draw_round_rect_alpha(min_x, control_y, control_size, control_size, chip_radius, RGB(246, 252, 255), focused ? 58 : 34);
    gfx_fill_rect(min_x + 7, control_y + 13, control_size - 14, 2, RGB(255, 246, 214));

    gfx_blur_round_rect(max_x, control_y, control_size, control_size, chip_radius);
    gfx_refract_round_rect_edges(max_x, control_y, control_size, control_size, chip_radius, 1);
    gfx_fill_round_rect_plain_alpha(max_x, control_y, control_size, control_size, chip_radius, RGB(83, 196, 101), focused ? 32 : 16);
    gfx_draw_round_rect_alpha(max_x, control_y, control_size, control_size, chip_radius, RGB(246, 252, 255), focused ? 58 : 34);
    gfx_draw_rect(max_x + 7, control_y + 7, control_size - 14, control_size - 14, RGB(224, 255, 230));

    gfx_blur_round_rect(close_x, control_y, control_size, control_size, chip_radius);
    gfx_refract_round_rect_edges(close_x, control_y, control_size, control_size, chip_radius, 1);
    gfx_fill_round_rect_plain_alpha(close_x, control_y, control_size, control_size, chip_radius, RGB(238, 94, 88), focused ? 32 : 16);
    gfx_draw_round_rect_alpha(close_x, control_y, control_size, control_size, chip_radius, RGB(246, 252, 255), focused ? 58 : 34);
    gfx_draw_line(close_x + 8, control_y + 8, close_x + control_size - 8, control_y + control_size - 8, RGB(255, 226, 224));
    gfx_draw_line(close_x + control_size - 8, control_y + 8, close_x + 8, control_y + control_size - 8, RGB(255, 226, 224));

    gfx_draw_line(window->x + window->width - 17, window->y + window->height - 6, window->x + window->width - 6, window->y + window->height - 17, RGB(132, 146, 154));
    gfx_draw_line(window->x + window->width - 12, window->y + window->height - 5, window->x + window->width - 5, window->y + window->height - 12, RGB(132, 146, 154));
}

static void draw_button(i32 x, i32 y, i32 width, const char *label, bool active) {
    draw_glass_chip(x, y, width, 28, label, active);
}

static void draw_file_explorer(i32 x, i32 y, i32 width, i32 height) {
    draw_glass_panel(x, y, width, height, 16);
    gfx_fill_round_rect_alpha(x + 8, y + 8, width - 16, 42, 14, RGB(255, 255, 255), 75);
    draw_button(x + 10, y + 10, 74, "NEW", false);
    draw_button(x + 92, y + 10, 86, "DELETE", false);
    draw_button(x + 188, y + 10, 74, "COPY", false);
    draw_button(x + 270, y + 10, 86, "RENAME", false);
    draw_button(x + 364, y + 10, 72, "MOVE", false);
    draw_button(x + 444, y + 10, 112, "SAVE DEMO", false);

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
        Color row = selected ? RGB(208, 224, 246) : RGB(255, 255, 255);
        gfx_fill_round_rect_alpha(x + 10, row_y - 3, list_w, row_h - 3, 7, row, selected ? 170 : 92);

        char size_text[24];
        u64_to_dec(file->size, size_text, sizeof(size_text));
        gfx_draw_text(x + 20, row_y + 2, file->name, selected ? RGB(22, 55, 94) : RGB(44, 53, 60), 1);
        gfx_draw_text(x + list_w - 54, row_y + 2, size_text, RGB(101, 113, 121), 1);
        row_y += row_h;
    }

    draw_glass_panel(preview_x, y + 58, width - list_w - 34, height - 72, 16);
    const FsFile *selected = fs_get_file((size_t)selected_file_index);
    if (selected) {
        gfx_draw_text(preview_x + 14, y + 74, selected->name, RGB(35, 43, 50), 1);
        gfx_fill_rect(preview_x + 14, y + 96, width - list_w - 62, 1, RGB(218, 226, 232));
        gfx_draw_text(preview_x + 14, y + 112, selected->contents[0] ? selected->contents : "(empty file)", RGB(71, 82, 90), 1);
        if (file_clipboard[0]) {
            gfx_draw_text(preview_x + 14, y + height - 36, "Clipboard:", RGB(88, 98, 108), 1);
            gfx_draw_text(preview_x + 96, y + height - 36, file_clipboard, RGB(48, 58, 68), 1);
        }
    }
}

static void draw_store_window(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    draw_glass_panel(x, y, width, height, 16);
    gfx_fill_round_rect_alpha(x + 14, y + 14, width - 28, 46, 18, theme->accent, 46);
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
        Color card = installed ? RGB(210, 246, 226) : RGB(255, 255, 255);
        gfx_liquid_glass_rect(x + 14, row_y - 2, width - 32, 72, 18);
        gfx_fill_round_rect_alpha(x + 14, row_y - 2, width - 32, 72, 18, card, installed ? 66 : 36);
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
    gfx_liquid_glass_rect(x, y, 112, 82, 18);
    gfx_fill_round_rect_alpha(x, y, 112, 82, 18, RGB(255, 255, 255), 24);
    gfx_fill_round_rect_alpha(x + 34, y + 12, 44, 34, 14, accent, 220);
    gfx_draw_text(x + 18, y + 54, name, RGB(35, 43, 52), 1);
    gfx_draw_text(x + 18, y + 68, kind, RGB(100, 110, 120), 1);
}

static void draw_launcher_window(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    draw_glass_panel(x, y, width, height, 16);
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
        gfx_liquid_glass_rect(x + 22, row_y, width - 56, 36, 14);
        gfx_fill_round_rect_alpha(x + 22, row_y, width - 56, 36, 14, RGB(255, 255, 255), 42);
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
    draw_glass_panel(x, y, width, height, 16);
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
    draw_glass_panel(x + 22, y + 102, width - 44, height - 126, 16);
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
    draw_glass_panel(x, y, width, height, 16);
    gfx_draw_text(x + 22, y + 18, "System Settings", theme->text, 2);
    gfx_draw_text(x + 24, y + 48, "Appearance, Apps, Storage, Platform, and About.", RGB(82, 94, 104), 1);
    gfx_liquid_glass_rect(x + 18, y + 68, width - 36, 26, 13);
    gfx_fill_round_rect_alpha(x + 18, y + 68, width - 36, 26, 13, RGB(255, 255, 255), 45);
    gfx_draw_text(x + 32, y + 76, "Appearance", theme->text, 1);
    gfx_draw_text(x + 134, y + 76, "Apps", RGB(82, 94, 104), 1);
    gfx_draw_text(x + 184, y + 76, "Storage", RGB(82, 94, 104), 1);
    gfx_draw_text(x + 254, y + 76, "Platform", RGB(82, 94, 104), 1);
    gfx_draw_text(x + 334, y + 76, "About", RGB(82, 94, 104), 1);

    i32 start_y = y + 98;
    i32 theme_w = width / 2 - 38;
    gfx_draw_text(x + 20, y + 96, "Appearance", theme->text, 1);
    for (size_t i = 0; i < sizeof(themes) / sizeof(themes[0]); i++) {
        const DesktopTheme *choice = &themes[i];
        i32 row_y = start_y + 18 + (i32)i * 46;
        bool selected = i == current_theme;
        gfx_liquid_glass_rect(x + 18, row_y, theme_w, 36, 14);
        gfx_fill_round_rect_alpha(x + 18, row_y, theme_w, 36, 14, selected ? choice->accent : RGB(255, 255, 255), selected ? 76 : 32);
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
    gfx_draw_text(manager_x, y + 96, "Apps", theme->text, 1);
    draw_glass_panel(manager_x, y + 116, manager_w, 118, 16);

    size_t visible = 0;
    i32 row_y = y + 124;
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
        gfx_draw_text(manager_x + 12, y + 136, "No Store apps installed yet.", RGB(92, 104, 116), 1);
    }

    char storage[64];
    char number[16];
    storage[0] = 0;
    append_text(storage, sizeof(storage), fs_persistence_available() ? "Persistence: disk-backed" : "Persistence: RAM only");
    gfx_draw_text(x + 20, y + 316, storage, RGB(82, 94, 104), 1);
    storage[0] = 0;
    append_text(storage, sizeof(storage), "LiquidFS: ");
    u64_to_dec(fs_file_count(), number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), "/");
    u64_to_dec(fs_capacity(), number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), " files used");
    gfx_draw_text(x + 20, y + 336, storage, RGB(82, 94, 104), 1);

    PlatformSummary summary = platform_summary();
    storage[0] = 0;
    append_text(storage, sizeof(storage), "Platform: ");
    u64_to_dec(summary.available, number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), " ready / ");
    u64_to_dec(summary.partial, number, sizeof(number));
    append_text(storage, sizeof(storage), number);
    append_text(storage, sizeof(storage), " partial");
    gfx_draw_text(x + 20, y + 356, storage, RGB(82, 94, 104), 1);

    gfx_draw_text(manager_x, y + 246, "Task Manager", theme->text, 1);
    draw_glass_panel(manager_x, y + 266, manager_w, 104, 16);
    size_t rows = process_count();
    if (rows > 3) {
        rows = 3;
    }
    for (size_t i = 0; i < rows; i++) {
        const Process *process = process_get(i);
        if (!process) {
            continue;
        }
        i32 process_y = y + 274 + (i32)i * 28;
        char pid_text[16];
        char ticks_text[16];
        u64_to_dec(process->pid, pid_text, sizeof(pid_text));
        u64_to_dec(process->ticks, ticks_text, sizeof(ticks_text));
        gfx_draw_text(manager_x + 12, process_y + 8, pid_text, RGB(66, 76, 86), 1);
        gfx_draw_text(manager_x + 42, process_y + 8, process->name, theme->text, 1);
        gfx_draw_text(manager_x + 126, process_y + 8, process_state_name(process->state), RGB(82, 94, 104), 1);
        gfx_draw_text(manager_x + 196, process_y + 8, ticks_text, RGB(82, 94, 104), 1);
        if (process->mode == PROCESS_USER && process->state != PROCESS_STOPPED && process->state != PROCESS_CRASHED) {
            gfx_fill_round_rect_alpha(manager_x + manager_w - 58, process_y + 2, 46, 20, 8, RGB(180, 74, 82), 218);
            gfx_draw_text(manager_x + manager_w - 52, process_y + 8, "FORCE", RGB(255, 255, 255), 1);
        }
    }
    gfx_draw_text(manager_x, y + 382, app_status_text, RGB(82, 94, 104), 1);
}

static void draw_control_center_window(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    draw_glass_panel(x, y, width, height, 16);
    gfx_draw_text(x + 22, y + 22, "Control Center", theme->text, 2);
    gfx_draw_text(x + 24, y + 52, "Quick controls for the LiquidOS shell.", RGB(82, 94, 104), 1);

    gfx_liquid_glass_rect(x + 22, y + 64, 132, 42, 16);
    gfx_fill_round_rect_alpha(x + 22, y + 64, 132, 42, 16, wifi_enabled ? RGB(210, 244, 225) : RGB(244, 226, 226), 92);
    gfx_draw_text(x + 38, y + 78, wifi_enabled ? "Wi-Fi On" : "Wi-Fi Off", RGB(35, 50, 58), 1);
    gfx_liquid_glass_rect(x + 166, y + 64, 132, 42, 16);
    gfx_fill_round_rect_alpha(x + 166, y + 64, 132, 42, 16, battery_saver ? RGB(255, 240, 205) : RGB(232, 240, 250), 92);
    gfx_draw_text(x + 182, y + 78, battery_saver ? "Battery Save" : "Full Power", RGB(35, 50, 58), 1);

    draw_glass_panel(x + 22, y + 124, width - 44, 58, 18);
    gfx_draw_text(x + 40, y + 138, themes[current_theme].name, theme->text, 1);
    gfx_draw_text(x + 40, y + 158, fs_persistence_available() ? "Storage: disk-backed" : "Storage: RAM only", RGB(82, 94, 104), 1);

    PlatformSummary summary = platform_summary();
    char line[64];
    char number[16];
    line[0] = 0;
    append_text(line, sizeof(line), "Platform: ");
    u64_to_dec(summary.available, number, sizeof(number));
    append_text(line, sizeof(line), number);
    append_text(line, sizeof(line), " ready, ");
    u64_to_dec(summary.partial, number, sizeof(number));
    append_text(line, sizeof(line), number);
    append_text(line, sizeof(line), " partial");
    gfx_draw_text(x + 40, y + 176, line, RGB(82, 94, 104), 1);

    gfx_draw_text(x + 24, y + 196, "Notifications", theme->text, 1);
    i32 note_y = y + 216;
    size_t max_notes = notification_count < 3 ? notification_count : 3;
    for (size_t i = 0; i < max_notes; i++) {
        size_t index = (notification_head + sizeof(notifications) / sizeof(notifications[0]) - 1 - i) %
                       (sizeof(notifications) / sizeof(notifications[0]));
        Notification *note = &notifications[index];
        gfx_liquid_glass_rect(x + 22, note_y, width - 44, 30, 12);
        gfx_fill_round_rect_alpha(x + 22, note_y, width - 44, 30, 12, note->unread ? RGB(230, 240, 255) : RGB(255, 255, 255), 58);
        gfx_draw_text(x + 34, note_y + 8, note->title, theme->text, 1);
        gfx_draw_text(x + 126, note_y + 8, note->body, RGB(82, 94, 104), 1);
        note_y += 34;
    }
    if (max_notes == 0) {
        gfx_draw_text(x + 34, y + 220, "No notifications yet.", RGB(82, 94, 104), 1);
    }

    gfx_fill_round_rect_alpha(x + 22, y + height - 48, 112, 34, 12, RGB(232, 238, 246), 240);
    gfx_draw_text(x + 50, y + height - 36, "Restart", RGB(35, 50, 58), 1);
    gfx_fill_round_rect_alpha(x + 146, y + height - 48, 132, 34, 12, RGB(248, 220, 224), 240);
    gfx_draw_text(x + 178, y + height - 36, "Shutdown", RGB(92, 30, 40), 1);
}

static void draw_notifications(void) {
    if (notification_count == 0) {
        return;
    }

    size_t index = (notification_head + sizeof(notifications) / sizeof(notifications[0]) - 1) %
                   (sizeof(notifications) / sizeof(notifications[0]));
    Notification *note = &notifications[index];
    if (!note->unread) {
        return;
    }

    i32 width = 330;
    i32 height = 68;
    i32 x = (i32)gfx_width() - width - 28;
    i32 y = (i32)gfx_height() - height - 28;
    draw_glass_panel(x, y, width, height, 18);
    gfx_fill_round_rect_alpha(x + 12, y + 12, 42, 42, 14, themes[current_theme].accent, 150);
    gfx_draw_text(x + 66, y + 16, note->title, themes[current_theme].text, 1);
    gfx_draw_text(x + 66, y + 36, note->body, RGB(82, 94, 104), 1);
}

static void draw_desktop_context_menu(void) {
    if (!context_menu_open) {
        return;
    }

    i32 x = context_menu_x;
    i32 y = context_menu_y;
    if (x + 156 > (i32)gfx_width()) {
        x = (i32)gfx_width() - 156;
    }
    if (y + 116 > (i32)gfx_height()) {
        y = (i32)gfx_height() - 116;
    }

    draw_glass_panel(x, y, 156, 116, 16);
    gfx_draw_text(x + 16, y + 12, "Launch Apps", RGB(35, 43, 52), 1);
    gfx_draw_text(x + 16, y + 40, "Open Files", RGB(35, 43, 52), 1);
    gfx_draw_text(x + 16, y + 68, "Settings", RGB(35, 43, 52), 1);
    gfx_draw_text(x + 16, y + 96, "New File", RGB(35, 43, 52), 1);
}

static void draw_window(WindowKind kind) {
    Window *window = &windows[kind];
    if (!window->open || window->minimized) {
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
    } else if (kind == WINDOW_CONTROL_CENTER) {
        draw_control_center_window(content_x, content_y, content_w, content_h);
    }
}

static void draw_taskbar(void) {
    TaskbarLayout bar;
    taskbar_layout(&bar);
    i32 compact = gfx_width() < 900 ? 1 : 0;
    i32 radius = dock_scale_value(compact ? 27 : 32);
    i32 tile_size = dock_tile_size();
    i32 child_radius = dock_child_radius(dock_scale_value(compact ? 48 : 54), bar.h, radius);
    i32 icon_size = tile_size - (compact ? 9 : 10);

    draw_dock_glass_capsule(bar.x, bar.y, bar.w, bar.h, radius);
    draw_dock_clock(&bar);
    draw_dock_divider(&bar);

    for (i32 i = 0; i < bar.slots_available; i++) {
        const DockApp *app = &dock_apps[i];
        draw_dock_icon_asset(bar.slot_x + i * bar.slot_step, bar.slot_y,
                             tile_size, child_radius, icon_size,
                             app->pixels, app->icon_width, app->icon_height,
                             app->tile_top, app->tile_bottom, app->small_artwork);
    }
    if (dock_grip_visible || dock_resizing || hover_zone == 0 || hover_zone == 2) {
        draw_dock_resize_grip(&bar);
    }
}

void ui_init(const BootInfo *boot) {
    (void)boot;

    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();

    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;
    previous_mouse_x = mouse_x;
    previous_mouse_y = mouse_y;
    dock_scale_percent = 100;
    dock_resize_start_scale = 100;

    update_clock_text();
    gfx_prepare_wallpaper_rgb565(background_image_rgb565, BACKGROUND_IMAGE_WIDTH, BACKGROUND_IMAGE_HEIGHT);
    load_theme_setting();

    windows[WINDOW_TERMINAL] = make_window(90, 160, 720, 410, "Terminal");
    windows[WINDOW_BROWSER] = make_window(180, 180, 820, 500, "Liqueia");
    windows[WINDOW_FILES] = make_window(320, 260, 660, 400, "Files");
    windows[WINDOW_STORE] = make_window(240, 170, 720, 430, "Store");
    windows[WINDOW_SETTINGS] = make_window(280, 210, 660, 390, "System Settings");
    windows[WINDOW_LAUNCHER] = make_window(260, 150, 600, 410, "Launch Apps");
    windows[WINDOW_APP_VIEW] = make_window(300, 190, 620, 390, "App");
    windows[WINDOW_CONTROL_CENTER] = make_window(screen_w - 380, taskbar_y() + taskbar_h() + 16, 340, 300, "Control Center");

    if (screen_w < 800) {
        windows[WINDOW_TERMINAL] = make_window(25, 100, screen_w - 50, 310, "Terminal");
        windows[WINDOW_BROWSER] = make_window(40, 120, screen_w - 80, 320, "Liqueia");
        windows[WINDOW_FILES] = make_window(55, 140, screen_w - 110, 270, "Files");
        windows[WINDOW_STORE] = make_window(35, 110, screen_w - 70, 330, "Store");
        windows[WINDOW_SETTINGS] = make_window(45, 130, screen_w - 90, 330, "System Settings");
        windows[WINDOW_LAUNCHER] = make_window(30, 105, screen_w - 60, 330, "Launch Apps");
        windows[WINDOW_APP_VIEW] = make_window(50, 125, screen_w - 100, 315, "App");
        windows[WINDOW_CONTROL_CENTER] = make_window(35, 105, screen_w - 70, 300, "Control Center");
    }

    clamp_window(&windows[WINDOW_TERMINAL]);
    clamp_window(&windows[WINDOW_BROWSER]);
    clamp_window(&windows[WINDOW_FILES]);
    clamp_window(&windows[WINDOW_STORE]);
    clamp_window(&windows[WINDOW_SETTINGS]);
    clamp_window(&windows[WINDOW_LAUNCHER]);
    clamp_window(&windows[WINDOW_APP_VIEW]);
    clamp_window(&windows[WINDOW_CONTROL_CENTER]);
    for (i32 i = 0; i < WINDOW_COUNT; i++) {
        windows[i].restore_x = windows[i].x;
        windows[i].restore_y = windows[i].y;
        windows[i].restore_width = windows[i].width;
        windows[i].restore_height = windows[i].height;
    }

    z_order[0] = WINDOW_TERMINAL;
    z_order[1] = WINDOW_FILES;
    z_order[2] = WINDOW_STORE;
    z_order[3] = WINDOW_SETTINGS;
    z_order[4] = WINDOW_LAUNCHER;
    z_order[5] = WINDOW_APP_VIEW;
    z_order[6] = WINDOW_CONTROL_CENTER;
    z_order[7] = WINDOW_BROWSER;
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
    bool right_now = event->right_down;

    if (right_now && !previous_right &&
        !point_in_rect(mouse_x, mouse_y, taskbar_x(), taskbar_y(), taskbar_w(), taskbar_h()) &&
        !point_in_open_window(mouse_x, mouse_y)) {
        context_menu_x = mouse_x;
        context_menu_y = mouse_y;
        if (context_menu_x + 156 > (i32)gfx_width()) {
            context_menu_x = (i32)gfx_width() - 156;
        }
        if (context_menu_y + 116 > (i32)gfx_height()) {
            context_menu_y = (i32)gfx_height() - 116;
        }
        if (context_menu_x < 0) {
            context_menu_x = 0;
        }
        if (context_menu_y < 0) {
            context_menu_y = 0;
        }
        context_menu_open = true;
        mark_dirty_full();
    }

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

    if (dock_resizing && left_now && mouse_moved) {
        i32 next_scale = dock_resize_start_scale + (mouse_x - dock_resize_start_x) / 3;
        if (next_scale < 82) {
            next_scale = 82;
        }
        if (next_scale > 135) {
            next_scale = 135;
        }
        if (next_scale != dock_scale_percent) {
            mark_dirty_full();
            dock_scale_percent = next_scale;
            cursor_redraw_needed = true;
        }
    }

    if (left_now && !previous_left) {
        if (context_menu_open && point_in_rect(mouse_x, mouse_y, context_menu_x, context_menu_y, 156, 116)) {
            handle_desktop_click();
            previous_left = left_now;
            previous_right = right_now;
            return;
        }
        if (context_menu_open) {
            context_menu_open = false;
            mark_dirty_full();
        }
        if (point_in_rect(mouse_x, mouse_y, taskbar_x(), taskbar_y(), taskbar_w(), taskbar_h())) {
            handle_taskbar_click();
            mark_dirty_taskbar();
        } else {
            if (point_in_open_window(mouse_x, mouse_y)) {
                handle_window_click();
            } else {
                handle_desktop_click();
            }
        }
    }

    if (!left_now) {
        dragging = false;
        resizing = false;
        dock_resizing = false;
        if (dock_grip_visible) {
            dock_grip_visible = false;
            mark_dirty_taskbar();
        }
    }

    previous_left = left_now;
    previous_right = right_now;
}

void ui_update(u64 tick_count) {
    if (tick_count - last_clock_tick >= 100) {
        char old_clock[6];
        char old_date[11];
        strcpy(old_clock, clock_text);
        strcpy(old_date, date_text);
        update_clock_text();
        last_clock_tick = tick_count;
        if (strcmp(old_clock, clock_text) != 0 || strcmp(old_date, date_text) != 0) {
            mark_dirty_taskbar();
        }
    }

    for (size_t i = 0; i < notification_count; i++) {
        size_t index = (notification_head + sizeof(notifications) / sizeof(notifications[0]) - notification_count + i) %
                       (sizeof(notifications) / sizeof(notifications[0]));
        Notification *note = &notifications[index];
        if (note->unread && tick_count - note->created_tick > 420) {
            note->unread = false;
            mark_dirty_full();
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

        draw_desktop_context_menu();
        draw_taskbar();
        draw_notifications();
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
