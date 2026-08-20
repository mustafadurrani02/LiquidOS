#include <liquidos/fs.h>
#include <liquidos/app_store.h>
#include <liquidos/gfx.h>
#include <liquidos/input.h>
#include <liquidos/io.h>
#include <liquidos/lib.h>
#include <liquidos/liqueia.h>
#include <liquidos/network.h>
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

typedef struct FileExplorerEntry {
    char name[FS_NAME_LENGTH];
    char path[FS_NAME_LENGTH];
    bool folder;
    bool selected;
    u64 size;
    u64 modified_tick;
} FileExplorerEntry;

typedef struct FileExplorerLayout {
    i32 x;
    i32 y;
    i32 width;
    i32 height;
    i32 sidebar_w;
    i32 main_x;
    i32 main_w;
    i32 preview_x;
    i32 preview_w;
    i32 nav_y;
    i32 command_y;
    i32 body_y;
    i32 body_h;
    i32 address_x;
    i32 address_w;
    i32 search_x;
    i32 search_w;
    i32 list_y;
    i32 row_h;
} FileExplorerLayout;

typedef enum FileClipboardMode {
    FILE_CLIPBOARD_EMPTY = 0,
    FILE_CLIPBOARD_COPY,
    FILE_CLIPBOARD_CUT
} FileClipboardMode;

typedef enum FileCommand {
    FILE_CMD_BACK = 0,
    FILE_CMD_FORWARD,
    FILE_CMD_UP,
    FILE_CMD_NEW_FOLDER,
    FILE_CMD_COPY,
    FILE_CMD_CUT,
    FILE_CMD_PASTE,
    FILE_CMD_RENAME,
    FILE_CMD_DELETE,
    FILE_CMD_REFRESH,
    FILE_CMD_SORT,
    FILE_CMD_VIEW,
    FILE_CMD_FILTER,
    FILE_CMD_DETAILS,
    FILE_CMD_COUNT
} FileCommand;

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
    { WINDOW_SETTINGS, "SETTINGS", NULL, 0, 0, RGB(104, 130, 164), RGB(54, 61, 78), false },
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
static bool fullscreen_chrome_visible = false;
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
static UiPerformanceStats ui_stats;
static FileExplorerEntry files_entries[40];
static char files_current_path[FS_NAME_LENGTH] = "HOME";
static char files_back_stack[8][FS_NAME_LENGTH];
static char files_forward_stack[8][FS_NAME_LENGTH];
static char files_clipboard_path[FS_NAME_LENGTH] = "";
static char files_clipboard_paths[8][FS_NAME_LENGTH];
static bool files_clipboard_folders[8];
static char files_search[32] = "";
static FileClipboardMode files_clipboard_mode = FILE_CLIPBOARD_EMPTY;
static size_t files_entry_count = 0;
static size_t files_back_count = 0;
static size_t files_forward_count = 0;
static size_t files_clipboard_count = 0;
static i32 selected_file_index = 0;
static bool files_dirty = true;
static bool files_search_editing = false;
static bool files_sort_desc = false;
static bool files_grid_view = false;
static bool files_filter_folders = false;
static bool files_details_visible = false;
static u32 new_file_counter = 1;
static u32 new_folder_counter = 1;
static size_t current_theme = 0;
static size_t selected_store_app = 0;
static i32 settings_page = 0;
static bool settings_search_editing = false;
static char settings_search[32] = "";
static bool settings_bluetooth_enabled = false;
static bool settings_sound_enabled = true;
static bool settings_notifications_enabled = true;
static bool settings_focus_enabled = false;
static bool settings_night_light = false;
static bool settings_large_text = false;
static bool settings_reduce_motion = false;
static bool settings_high_contrast = false;
static bool settings_privacy_lock = false;
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

static size_t file_location_count(void) {
    return 10;
}

static bool path_is_home(const char *path) {
    return (path[0] == 'H' || path[0] == 'h') &&
           (path[1] == 'O' || path[1] == 'o') &&
           (path[2] == 'M' || path[2] == 'm') &&
           (path[3] == 'E' || path[3] == 'e') &&
           path[4] == 0;
}

static bool path_is_desktop(const char *path) {
    return path[0] == 'D' && path[1] == 'E' && path[2] == 'S' && path[3] == 'K' &&
           path[4] == 'T' && path[5] == 'O' && path[6] == 'P' && path[7] == 0;
}

static bool path_is_documents(const char *path) {
    return path[0] == 'D' && path[1] == 'O' && path[2] == 'C' && path[3] == 'U' &&
           path[4] == 'M' && path[5] == 'E' && path[6] == 'N' && path[7] == 'T' &&
           path[8] == 'S' && path[9] == 0;
}

static bool path_is_downloads(const char *path) {
    return path[0] == 'D' && path[1] == 'O' && path[2] == 'W' && path[3] == 'N' &&
           path[4] == 'L' && path[5] == 'O' && path[6] == 'A' && path[7] == 'D' &&
           path[8] == 'S' && path[9] == 0;
}

static bool path_is_pictures(const char *path) {
    return path[0] == 'P' && path[1] == 'I' && path[2] == 'C' && path[3] == 'T' &&
           path[4] == 'U' && path[5] == 'R' && path[6] == 'E' && path[7] == 'S' &&
           path[8] == 0;
}

static bool path_is_music(const char *path) {
    return path[0] == 'M' && path[1] == 'U' && path[2] == 'S' && path[3] == 'I' &&
           path[4] == 'C' && path[5] == 0;
}

static bool path_is_videos(const char *path) {
    return path[0] == 'V' && path[1] == 'I' && path[2] == 'D' && path[3] == 'E' &&
           path[4] == 'O' && path[5] == 'S' && path[6] == 0;
}

static bool path_is_apps(const char *path) {
    return path[0] == 'A' && path[1] == 'P' && path[2] == 'P' && path[3] == 'S' && path[4] == 0;
}

static bool path_is_system(const char *path) {
    return path[0] == 'S' && path[1] == 'Y' && path[2] == 'S' && path[3] == 'T' &&
           path[4] == 'E' && path[5] == 'M' && path[6] == 0;
}

static bool path_is_trash(const char *path) {
    return path[0] == 'T' && path[1] == 'R' && path[2] == 'A' && path[3] == 'S' &&
           path[4] == 'H' && path[5] == 0;
}

static bool file_location_matches(size_t index, const char *path) {
    if (!path) return false;
    if (index == 0) return path_is_home(path);
    if (index == 1) return path_is_desktop(path);
    if (index == 2) return path_is_documents(path);
    if (index == 3) return path_is_downloads(path);
    if (index == 4) return path_is_pictures(path);
    if (index == 5) return path_is_music(path);
    if (index == 6) return path_is_videos(path);
    if (index == 7) return path_is_apps(path);
    if (index == 8) return path_is_system(path);
    if (index == 9) return path_is_trash(path);
    return false;
}

static i32 file_command_width(FileCommand command) {
    if (command == FILE_CMD_BACK) return 30;
    if (command == FILE_CMD_FORWARD) return 30;
    if (command == FILE_CMD_UP) return 30;
    if (command == FILE_CMD_NEW_FOLDER) return 50;
    if (command == FILE_CMD_COPY) return 48;
    if (command == FILE_CMD_CUT) return 40;
    if (command == FILE_CMD_PASTE) return 54;
    if (command == FILE_CMD_RENAME) return 64;
    if (command == FILE_CMD_DELETE) return 50;
    if (command == FILE_CMD_REFRESH) return 30;
    if (command == FILE_CMD_SORT) return 54;
    if (command == FILE_CMD_VIEW) return 54;
    if (command == FILE_CMD_FILTER) return 58;
    if (command == FILE_CMD_DETAILS) return 62;
    return 0;
}

static i32 file_button_height(void);

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

static i32 dock_reference_h(void) {
    return dock_scale_value(gfx_width() < 900 ? 86 : 105);
}

static i32 dock_reference_radius(void) {
    return dock_scale_value(gfx_width() < 900 ? 33 : 40);
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
    i32 icon_gap_count = count > 0 ? count - 1 : 0;
    i32 content_width = dock_clock_w() + gap + tile_size * count + gap * icon_gap_count;
    i32 width = content_width + gap * 2;
    i32 max_width = screen_w - 32;
    return width > max_width ? max_width : width;
}

static i32 taskbar_h(void) {
    return dock_reference_h();
}

static void dock_grip_rect(const TaskbarLayout *bar, i32 *x, i32 *y, i32 *size) {
    i32 grip = dock_scale_value(16);
    *x = bar->x + bar->w - grip - dock_scale_value(5);
    *y = bar->y + bar->h - grip - dock_scale_value(5);
    *size = grip;
}

static void taskbar_layout(TaskbarLayout *layout) {
    i32 gap = dock_gap();
    i32 count = dock_app_count();

    layout->x = taskbar_x();
    layout->y = taskbar_y();
    layout->w = taskbar_w();
    layout->h = taskbar_h();

    layout->slot_w = dock_tile_size();
    layout->slot_h = dock_tile_size();
    layout->slot_step = layout->slot_w + gap;
    layout->slots_available = count;
    layout->clock_w = dock_clock_w();
    i32 icon_gap_count = count > 0 ? count - 1 : 0;
    i32 group_w = layout->clock_w + gap + layout->slot_w * count + gap * icon_gap_count;
    i32 content_pad = (layout->w - group_w) / 2;
    if (content_pad < gap) {
        content_pad = gap;
    }
    layout->clock_x = layout->x + content_pad;
    layout->clock_y = layout->y + (layout->h - layout->slot_h) / 2;
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
    if (window->expanded) {
        mark_dirty_full();
        return;
    }
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
            mark_dirty_window(old_focus);
            mark_dirty_window(kind);
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
    mark_dirty_window(old_focus);
    mark_dirty_window(kind);
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

    if (window->expanded) {
        window->x = 0;
        window->y = 0;
        window->width = screen_w;
        window->height = screen_h;
        return;
    }

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
    mark_dirty_full();
    if (!window->expanded) {
        remember_window_restore(window);
        window->x = 0;
        window->y = 0;
        window->width = (i32)gfx_width();
        window->height = (i32)gfx_height();
        window->expanded = true;
        fullscreen_chrome_visible = false;
    } else {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->width = window->restore_width;
        window->height = window->restore_height;
        window->expanded = false;
        fullscreen_chrome_visible = false;
        clamp_window(window);
    }
    mark_dirty_full();
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

static char ascii_lower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
}

static bool starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool starts_with_ci(const char *text, const char *prefix) {
    while (*prefix) {
        if (ascii_lower(*text) != ascii_lower(*prefix)) {
            return false;
        }
        text++;
        prefix++;
    }
    return true;
}

static bool contains_text_ci(const char *text, const char *needle) {
    if (!needle[0]) {
        return true;
    }
    while (*text) {
        if (starts_with_ci(text, needle)) {
            return true;
        }
        text++;
    }
    return false;
}

static const char *last_path_segment(const char *path) {
    const char *base = path;
    while (*path) {
        if (*path == '/') {
            base = path + 1;
        }
        path++;
    }
    return base;
}

static void parent_path(const char *path, char *out, size_t out_size) {
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] != '/') {
        len--;
    }
    if (len > 0) {
        len--;
    }
    if (len + 1 > out_size) {
        len = out_size - 1;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = path[i];
    }
    out[len] = 0;
}

static void join_path(const char *folder, const char *name, char *out, size_t out_size) {
    out[0] = 0;
    if (folder && folder[0]) {
        append_text(out, out_size, folder);
        append_text(out, out_size, "/");
    }
    append_text(out, out_size, name);
}

static bool path_in_folder(const char *path, const char *folder, const char **rest) {
    if (!folder[0]) {
        *rest = path;
        return true;
    }
    size_t len = strlen(folder);
    if (strncmp(path, folder, len) != 0 || path[len] != '/') {
        return false;
    }
    *rest = path + len + 1;
    return true;
}

static bool path_has_child_separator(const char *text) {
    while (*text) {
        if (*text == '/') {
            return true;
        }
        text++;
    }
    return false;
}

static bool is_dir_marker_name(const char *name) {
    return name[0] == '.' && name[1] == 'D' && name[2] == 'I' && name[3] == 'R' && name[4] == 0;
}

static void dir_marker_path(const char *folder, char *out, size_t out_size) {
    join_path(folder, ".DIR", out, out_size);
}

static bool files_is_folder_path(const char *path) {
    char marker[FS_NAME_LENGTH];
    dir_marker_path(path, marker, sizeof(marker));
    if (fs_find(marker)) {
        return true;
    }

    char prefix[FS_NAME_LENGTH];
    prefix[0] = 0;
    append_text(prefix, sizeof(prefix), path);
    append_text(prefix, sizeof(prefix), "/");
    for (size_t i = 0; i < fs_file_count(); i++) {
        const FsFile *file = fs_get_file(i);
        if (file && starts_with(file->name, prefix)) {
            return true;
        }
    }
    for (size_t i = 0; i < file_location_count(); i++) {
        if (file_location_matches(i, path)) {
            return true;
        }
    }
    return false;
}

static const char *file_type_for_name(const char *name, bool folder) {
    if (folder) {
        return "Folder";
    }
    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            dot = p;
        }
    }
    if (!dot) {
        return "File";
    }
    if (dot[0] == '.' && dot[1] == 'T' && dot[2] == 'X' && dot[3] == 'T' && dot[4] == 0) {
        return "Text";
    }
    if (dot[0] == '.' && dot[1] == 'A' && dot[2] == 'P' && dot[3] == 'P' && dot[4] == 0) {
        return "App";
    }
    if (dot[0] == '.' && dot[1] == 'H' && dot[2] == 'T' && dot[3] == 'M' && dot[4] == 'L' && dot[5] == 0) {
        return "Web";
    }
    if (dot[0] == '.' && dot[1] == 'L' && dot[2] == 'P' && dot[3] == 'K' && dot[4] == 'G' && dot[5] == 0) {
        return "Package";
    }
    return dot + 1;
}

static void files_mark_dirty(void) {
    files_dirty = true;
}

static bool files_entry_duplicate(const char *path, bool folder) {
    for (size_t i = 0; i < files_entry_count; i++) {
        if (files_entries[i].folder == folder && strcmp(files_entries[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static void files_add_entry(const char *name, const char *path, bool folder, u64 size, u64 modified_tick) {
    if (files_entry_count >= sizeof(files_entries) / sizeof(files_entries[0]) ||
        files_entry_duplicate(path, folder)) {
        return;
    }
    if (files_filter_folders && !folder) {
        return;
    }
    if (files_search[0] && !contains_text_ci(name, files_search) && !contains_text_ci(path, files_search)) {
        return;
    }

    FileExplorerEntry *entry = &files_entries[files_entry_count++];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->folder = folder;
    entry->size = folder ? 0 : size;
    entry->modified_tick = modified_tick;
}

static void files_add_root_location_entry(size_t index) {
    if (index == 0) { files_add_entry("Home", "HOME", true, 0, 1); return; }
    if (index == 1) { files_add_entry("Desktop", "DESKTOP", true, 0, 1); return; }
    if (index == 2) { files_add_entry("Documents", "DOCUMENTS", true, 0, 1); return; }
    if (index == 3) { files_add_entry("Downloads", "DOWNLOADS", true, 0, 1); return; }
    if (index == 4) { files_add_entry("Pictures", "PICTURES", true, 0, 1); return; }
    if (index == 5) { files_add_entry("Music", "MUSIC", true, 0, 1); return; }
    if (index == 6) { files_add_entry("Videos", "VIDEOS", true, 0, 1); return; }
    if (index == 7) { files_add_entry("Applications", "APPS", true, 0, 1); return; }
    if (index == 8) { files_add_entry("System", "SYSTEM", true, 0, 1); return; }
    if (index == 9) { files_add_entry("Trash", "TRASH", true, 0, 1); return; }
}

static i32 files_compare_entries(const FileExplorerEntry *left, const FileExplorerEntry *right) {
    if (left->folder != right->folder) {
        return left->folder ? -1 : 1;
    }
    i32 cmp = strcmp(left->name, right->name);
    return files_sort_desc ? -cmp : cmp;
}

static void files_sort_entries(void) {
    for (size_t i = 0; i < files_entry_count; i++) {
        for (size_t j = i + 1; j < files_entry_count; j++) {
            if (files_compare_entries(&files_entries[i], &files_entries[j]) > 0) {
                FileExplorerEntry temp = files_entries[i];
                files_entries[i] = files_entries[j];
                files_entries[j] = temp;
            }
        }
    }
}

static void files_rebuild_entries(void) {
    files_entry_count = 0;
    if (!files_current_path[0]) {
        for (size_t i = 0; i < file_location_count(); i++) {
            files_add_root_location_entry(i);
        }
    }

    for (size_t i = 0; i < fs_file_count(); i++) {
        const FsFile *file = fs_get_file(i);
        const char *rest = NULL;
        if (!file || !path_in_folder(file->name, files_current_path, &rest) || !rest[0]) {
            continue;
        }

        char name[FS_NAME_LENGTH];
        size_t used = 0;
        while (rest[used] && rest[used] != '/' && used + 1 < sizeof(name)) {
            name[used] = rest[used];
            used++;
        }
        name[used] = 0;
        if (!name[0] || is_dir_marker_name(name)) {
            continue;
        }

        char path[FS_NAME_LENGTH];
        join_path(files_current_path, name, path, sizeof(path));
        if (path_has_child_separator(rest)) {
            files_add_entry(name, path, true, 0, file->modified_tick);
        } else {
            files_add_entry(name, file->name, false, file->size, file->modified_tick);
        }
    }
    files_sort_entries();

    if (selected_file_index >= (i32)files_entry_count) {
        selected_file_index = (i32)files_entry_count - 1;
    }
    if (selected_file_index < 0) {
        selected_file_index = 0;
    }
    files_dirty = false;
}

static void files_ensure_entries(void) {
    if (files_dirty) {
        files_rebuild_entries();
    }
}

static void files_clear_selection(void) {
    files_ensure_entries();
    for (size_t i = 0; i < files_entry_count; i++) {
        files_entries[i].selected = false;
    }
}

static void files_select_index(i32 index) {
    files_ensure_entries();
    if (index < 0 || index >= (i32)files_entry_count) {
        return;
    }
    files_clear_selection();
    selected_file_index = index;
    files_entries[index].selected = true;
}

static void files_toggle_index(i32 index) {
    files_ensure_entries();
    if (index < 0 || index >= (i32)files_entry_count) {
        return;
    }
    selected_file_index = index;
    files_entries[index].selected = !files_entries[index].selected;
}

static FileExplorerEntry *files_primary_entry(void) {
    files_ensure_entries();
    for (size_t i = 0; i < files_entry_count; i++) {
        if (files_entries[i].selected) {
            selected_file_index = (i32)i;
            return &files_entries[i];
        }
    }
    if (selected_file_index >= 0 && selected_file_index < (i32)files_entry_count) {
        return &files_entries[selected_file_index];
    }
    return NULL;
}

static bool files_any_selected(void) {
    files_ensure_entries();
    for (size_t i = 0; i < files_entry_count; i++) {
        if (files_entries[i].selected) {
            return true;
        }
    }
    return false;
}

static bool files_entry_should_act(size_t index, bool any_selected) {
    if (index >= files_entry_count) {
        return false;
    }
    return any_selected ? files_entries[index].selected : (i32)index == selected_file_index;
}

static void files_push_back(const char *path) {
    if (files_back_count >= sizeof(files_back_stack) / sizeof(files_back_stack[0])) {
        for (size_t i = 1; i < files_back_count; i++) {
            strcpy(files_back_stack[i - 1], files_back_stack[i]);
        }
        files_back_count--;
    }
    strncpy(files_back_stack[files_back_count], path, FS_NAME_LENGTH - 1);
    files_back_stack[files_back_count][FS_NAME_LENGTH - 1] = 0;
    files_back_count++;
}

static bool files_pop_stack(char stack[8][FS_NAME_LENGTH], size_t *count, char *out, size_t out_size) {
    if (*count == 0) {
        return false;
    }
    (*count)--;
    strncpy(out, stack[*count], out_size - 1);
    out[out_size - 1] = 0;
    return true;
}

static void files_navigate_to(const char *path, bool record_history) {
    if (strcmp(files_current_path, path) == 0) {
        return;
    }
    if (record_history) {
        files_push_back(files_current_path);
        files_forward_count = 0;
    }
    strncpy(files_current_path, path, sizeof(files_current_path) - 1);
    files_current_path[sizeof(files_current_path) - 1] = 0;
    selected_file_index = 0;
    files_clear_selection();
    files_mark_dirty();
    set_taskbar_message("FOLDER OPENED");
}

static void files_navigate_to_location(size_t index, bool record_history) {
    if (index == 0) { files_navigate_to("HOME", record_history); return; }
    if (index == 1) { files_navigate_to("DESKTOP", record_history); return; }
    if (index == 2) { files_navigate_to("DOCUMENTS", record_history); return; }
    if (index == 3) { files_navigate_to("DOWNLOADS", record_history); return; }
    if (index == 4) { files_navigate_to("PICTURES", record_history); return; }
    if (index == 5) { files_navigate_to("MUSIC", record_history); return; }
    if (index == 6) { files_navigate_to("VIDEOS", record_history); return; }
    if (index == 7) { files_navigate_to("APPS", record_history); return; }
    if (index == 8) { files_navigate_to("SYSTEM", record_history); return; }
    if (index == 9) { files_navigate_to("TRASH", record_history); return; }
}

static void make_untitled_name(char *out, size_t out_size) {
    char number[16];
    u64_to_dec(new_file_counter++, number, sizeof(number));
    out[0] = 0;
    append_text(out, out_size, "DESKTOP/UNTITLED");
    append_text(out, out_size, number);
    append_text(out, out_size, ".TXT");
}

static void make_unique_path(const char *folder, const char *base, const char *suffix, char *out, size_t out_size) {
    char candidate[FS_NAME_LENGTH];
    join_path(folder, base, candidate, sizeof(candidate));
    if (!fs_find(candidate) && !files_is_folder_path(candidate)) {
        strncpy(out, candidate, out_size - 1);
        out[out_size - 1] = 0;
        return;
    }
    for (u32 i = 1; i < 20; i++) {
        char number[8];
        char name[FS_NAME_LENGTH];
        u64_to_dec(i, number, sizeof(number));
        name[0] = 0;
        append_text(name, sizeof(name), base);
        append_text(name, sizeof(name), suffix);
        append_text(name, sizeof(name), number);
        join_path(folder, name, candidate, sizeof(candidate));
        if (!fs_find(candidate) && !files_is_folder_path(candidate)) {
            strncpy(out, candidate, out_size - 1);
            out[out_size - 1] = 0;
            return;
        }
    }
    strncpy(out, candidate, out_size - 1);
    out[out_size - 1] = 0;
}

static bool files_copy_path(const char *source, bool folder, const char *dest) {
    if (!folder) {
        return fs_copy(source, dest);
    }

    bool copied = false;
    char prefix[FS_NAME_LENGTH];
    prefix[0] = 0;
    append_text(prefix, sizeof(prefix), source);
    append_text(prefix, sizeof(prefix), "/");
    size_t original_count = fs_file_count();
    for (size_t i = 0; i < original_count; i++) {
        const FsFile *file = fs_get_file(i);
        if (!file || !starts_with(file->name, prefix)) {
            continue;
        }
        char new_path[FS_NAME_LENGTH];
        new_path[0] = 0;
        append_text(new_path, sizeof(new_path), dest);
        append_text(new_path, sizeof(new_path), "/");
        append_text(new_path, sizeof(new_path), file->name + strlen(prefix));
        copied = fs_copy(file->name, new_path) || copied;
    }
    if (!copied) {
        char marker[FS_NAME_LENGTH];
        dir_marker_path(dest, marker, sizeof(marker));
        copied = fs_write(marker, "LiquidOS directory marker.");
    }
    return copied;
}

static bool files_move_path(const char *source, bool folder, const char *dest) {
    if (!folder) {
        return fs_rename(source, dest);
    }

    bool moved = false;
    char prefix[FS_NAME_LENGTH];
    prefix[0] = 0;
    append_text(prefix, sizeof(prefix), source);
    append_text(prefix, sizeof(prefix), "/");
    size_t original_count = fs_file_count();
    for (size_t i = 0; i < original_count; i++) {
        const FsFile *file = fs_get_file(i);
        char old_path[FS_NAME_LENGTH];
        if (!file || !starts_with(file->name, prefix)) {
            continue;
        }
        strncpy(old_path, file->name, sizeof(old_path) - 1);
        old_path[sizeof(old_path) - 1] = 0;
        char new_path[FS_NAME_LENGTH];
        new_path[0] = 0;
        append_text(new_path, sizeof(new_path), dest);
        append_text(new_path, sizeof(new_path), "/");
        append_text(new_path, sizeof(new_path), old_path + strlen(prefix));
        moved = fs_rename(old_path, new_path) || moved;
    }
    return moved;
}

static bool files_delete_path(const char *path, bool folder) {
    if (!folder) {
        return fs_delete(path);
    }

    bool deleted = false;
    char prefix[FS_NAME_LENGTH];
    prefix[0] = 0;
    append_text(prefix, sizeof(prefix), path);
    append_text(prefix, sizeof(prefix), "/");

    for (;;) {
        char delete_name[FS_NAME_LENGTH];
        delete_name[0] = 0;
        for (size_t i = 0; i < fs_file_count(); i++) {
            const FsFile *file = fs_get_file(i);
            if (file && starts_with(file->name, prefix)) {
                strncpy(delete_name, file->name, sizeof(delete_name) - 1);
                delete_name[sizeof(delete_name) - 1] = 0;
                break;
            }
        }
        if (!delete_name[0]) {
            break;
        }
        deleted = fs_delete(delete_name) || deleted;
    }
    return deleted;
}

static void files_create_folder(void) {
    char folder_name[24];
    char number[12];
    u64_to_dec(new_folder_counter++, number, sizeof(number));
    folder_name[0] = 0;
    append_text(folder_name, sizeof(folder_name), "NEWFOLDER");
    append_text(folder_name, sizeof(folder_name), number);

    char folder[FS_NAME_LENGTH];
    char marker[FS_NAME_LENGTH];
    join_path(files_current_path, folder_name, folder, sizeof(folder));
    dir_marker_path(folder, marker, sizeof(marker));
    if (fs_write(marker, "LiquidOS directory marker.")) {
        set_taskbar_message("FOLDER CREATED");
        push_notification("Files", "Created a new folder.");
    } else {
        set_taskbar_message("NEW FOLDER FAILED");
    }
    files_mark_dirty();
}

static void files_copy_selection(bool cut) {
    files_ensure_entries();
    bool any_selected = files_any_selected();
    files_clipboard_count = 0;

    for (size_t i = 0; i < files_entry_count && files_clipboard_count < sizeof(files_clipboard_paths) / sizeof(files_clipboard_paths[0]); i++) {
        if (!files_entry_should_act(i, any_selected)) {
            continue;
        }
        strncpy(files_clipboard_paths[files_clipboard_count], files_entries[i].path, FS_NAME_LENGTH - 1);
        files_clipboard_paths[files_clipboard_count][FS_NAME_LENGTH - 1] = 0;
        files_clipboard_folders[files_clipboard_count] = files_entries[i].folder;
        files_clipboard_count++;
    }

    if (files_clipboard_count == 0) {
        return;
    }

    strncpy(files_clipboard_path, files_clipboard_paths[0], sizeof(files_clipboard_path) - 1);
    files_clipboard_path[sizeof(files_clipboard_path) - 1] = 0;
    files_clipboard_mode = cut ? FILE_CLIPBOARD_CUT : FILE_CLIPBOARD_COPY;
    set_taskbar_message(cut ? "CUT" : "COPIED");
}

static void files_paste_clipboard(void) {
    if (files_clipboard_mode == FILE_CLIPBOARD_EMPTY || files_clipboard_count == 0) {
        set_taskbar_message("CLIPBOARD EMPTY");
        return;
    }

    bool ok = false;
    for (size_t i = 0; i < files_clipboard_count; i++) {
        if (!files_clipboard_paths[i][0]) {
            continue;
        }
        char dest[FS_NAME_LENGTH];
        make_unique_path(files_current_path, last_path_segment(files_clipboard_paths[i]),
                         files_clipboard_mode == FILE_CLIPBOARD_COPY ? "-COPY" : "-MOVED",
                         dest, sizeof(dest));
        bool item_ok = files_clipboard_mode == FILE_CLIPBOARD_CUT ?
                       files_move_path(files_clipboard_paths[i], files_clipboard_folders[i], dest) :
                       files_copy_path(files_clipboard_paths[i], files_clipboard_folders[i], dest);
        ok = item_ok || ok;
    }
    if (ok) {
        set_taskbar_message(files_clipboard_mode == FILE_CLIPBOARD_CUT ? "MOVED" : "PASTED");
        push_notification("Files", files_clipboard_mode == FILE_CLIPBOARD_CUT ? "Moved item." : "Copied item.");
        if (files_clipboard_mode == FILE_CLIPBOARD_CUT) {
            files_clipboard_mode = FILE_CLIPBOARD_EMPTY;
            files_clipboard_path[0] = 0;
            files_clipboard_count = 0;
        }
    } else {
        set_taskbar_message("PASTE FAILED");
    }
    files_mark_dirty();
}

static void files_rename_selection(void) {
    FileExplorerEntry *entry = files_primary_entry();
    if (!entry) {
        return;
    }
    char parent[FS_NAME_LENGTH];
    char new_base[FS_NAME_LENGTH];
    char dest[FS_NAME_LENGTH];
    parent_path(entry->path, parent, sizeof(parent));
    new_base[0] = 0;
    append_text(new_base, sizeof(new_base), "RENAMED-");
    append_text(new_base, sizeof(new_base), entry->name);
    join_path(parent, new_base, dest, sizeof(dest));
    bool ok = entry->folder ? files_move_path(entry->path, true, dest) : fs_rename(entry->path, dest);
    set_taskbar_message(ok ? "RENAMED" : "RENAME FAILED");
    if (ok) {
        push_notification("Files", "Renamed selected item.");
    }
    files_mark_dirty();
}

static void files_delete_selection(void) {
    files_ensure_entries();
    bool any_selected = files_any_selected();
    bool any = false;
    for (size_t i = 0; i < files_entry_count; i++) {
        if (!files_entry_should_act(i, any_selected)) {
            continue;
        }
        FileExplorerEntry entry = files_entries[i];
        if (starts_with(entry.path, "TRASH/")) {
            any = files_delete_path(entry.path, entry.folder) || any;
            continue;
        }
        char dest[FS_NAME_LENGTH];
        make_unique_path("TRASH", entry.name, "-OLD", dest, sizeof(dest));
        any = (entry.folder ? files_move_path(entry.path, true, dest) : fs_rename(entry.path, dest)) || any;
    }
    set_taskbar_message(any ? "MOVED TO TRASH" : "DELETE FAILED");
    if (any) {
        push_notification("Files", "Moved selected item to Trash.");
    }
    files_mark_dirty();
}

static void files_go_back(void) {
    char path[FS_NAME_LENGTH];
    if (files_pop_stack(files_back_stack, &files_back_count, path, sizeof(path))) {
        if (files_forward_count < sizeof(files_forward_stack) / sizeof(files_forward_stack[0])) {
            strncpy(files_forward_stack[files_forward_count], files_current_path, FS_NAME_LENGTH - 1);
            files_forward_stack[files_forward_count][FS_NAME_LENGTH - 1] = 0;
            files_forward_count++;
        }
        files_navigate_to(path, false);
    }
}

static void files_go_forward(void) {
    char path[FS_NAME_LENGTH];
    if (files_pop_stack(files_forward_stack, &files_forward_count, path, sizeof(path))) {
        files_push_back(files_current_path);
        files_navigate_to(path, false);
    }
}

static void files_go_up(void) {
    char parent[FS_NAME_LENGTH];
    parent_path(files_current_path, parent, sizeof(parent));
    files_navigate_to(parent, true);
}

static void files_run_command(FileCommand command) {
    switch (command) {
    case FILE_CMD_BACK: files_go_back(); break;
    case FILE_CMD_FORWARD: files_go_forward(); break;
    case FILE_CMD_UP: files_go_up(); break;
    case FILE_CMD_NEW_FOLDER: files_create_folder(); break;
    case FILE_CMD_COPY: files_copy_selection(false); break;
    case FILE_CMD_CUT: files_copy_selection(true); break;
    case FILE_CMD_PASTE: files_paste_clipboard(); break;
    case FILE_CMD_RENAME: files_rename_selection(); break;
    case FILE_CMD_DELETE: files_delete_selection(); break;
    case FILE_CMD_REFRESH:
        files_mark_dirty();
        set_taskbar_message("REFRESHED");
        break;
    case FILE_CMD_SORT:
        files_sort_desc = !files_sort_desc;
        files_mark_dirty();
        set_taskbar_message(files_sort_desc ? "SORT Z-A" : "SORT A-Z");
        break;
    case FILE_CMD_VIEW:
        files_grid_view = !files_grid_view;
        set_taskbar_message(files_grid_view ? "GRID VIEW" : "LIST VIEW");
        break;
    case FILE_CMD_FILTER:
        files_filter_folders = !files_filter_folders;
        files_mark_dirty();
        set_taskbar_message(files_filter_folders ? "FOLDERS ONLY" : "FILTER OFF");
        break;
    case FILE_CMD_DETAILS:
        files_details_visible = !files_details_visible;
        set_taskbar_message(files_details_visible ? "DETAILS ON" : "DETAILS OFF");
        break;
    default:
        break;
    }
}

static void files_on_char(char ch) {
    if (!files_search_editing) {
        return;
    }
    size_t length = strlen(files_search);
    if (ch == '\n') {
        files_search_editing = false;
    } else if (ch == '\b') {
        if (length > 0) {
            files_search[--length] = 0;
        }
    } else if (ch >= 32 && ch <= 126 && length + 1 < sizeof(files_search)) {
        files_search[length] = ch;
        files_search[length + 1] = 0;
    }
    selected_file_index = 0;
    files_mark_dirty();
}

static void files_make_layout(i32 x, i32 y, i32 width, i32 height, FileExplorerLayout *layout) {
    layout->x = x;
    layout->y = y;
    layout->width = width;
    layout->height = height;
    layout->sidebar_w = width < 700 ? 120 : 148;
    layout->preview_w = (files_details_visible && width >= 760) ? 182 : 0;
    layout->main_x = x + layout->sidebar_w + 10;
    layout->main_w = width - layout->sidebar_w - layout->preview_w - 24;
    if (layout->main_w < 260) {
        layout->main_w = width - layout->sidebar_w - 18;
        layout->preview_w = 0;
    }
    layout->preview_x = x + width - layout->preview_w - 10;
    layout->nav_y = y + 10;
    layout->command_y = y + 48;
    layout->body_y = y + 84;
    layout->body_h = height - 96;
    layout->row_h = 36;
    layout->list_y = layout->body_y + 92;
    layout->search_w = width < 760 ? 142 : 190;
    layout->search_x = x + width - layout->search_w - 14;
    if (layout->preview_w > 0) {
        layout->search_x = layout->preview_x - layout->search_w - 10;
    }
    layout->address_x = x + 132;
    layout->address_w = layout->search_x - layout->address_x - 8;
    if (layout->address_w < 92) {
        layout->address_x = x + 112;
        layout->address_w = layout->search_x - layout->address_x - 8;
    }
    if (layout->address_w < 80) {
        layout->address_w = 80;
    }
}

static size_t files_quick_location(size_t index) {
    if (index == 0) return 1;
    if (index == 1) return 3;
    if (index == 2) return 2;
    if (index == 3) return 4;
    if (index == 4) return 5;
    if (index == 5) return 6;
    return 0;
}

static size_t files_quick_location_count(void) {
    return 6;
}

static bool files_command_bar_hit(const FileExplorerLayout *layout, i32 px, i32 py, FileCommand *out) {
    i32 bx = layout->x + 12;
    i32 details_w = file_command_width(FILE_CMD_DETAILS);
    i32 details_x = layout->x + layout->width - details_w - 14;
    i32 command_limit = details_x - 12;
    if (point_in_rect(px, py, details_x, layout->command_y, details_w, file_button_height())) {
        *out = FILE_CMD_DETAILS;
        return true;
    }
    #define FILES_TRY_BAR_COMMAND(command_value) \
        do { \
            FileCommand command = (command_value); \
            i32 width = file_command_width(command); \
            if (bx + width > command_limit) { \
                return false; \
            } \
            if (point_in_rect(px, py, bx, layout->command_y, width, file_button_height())) { \
                *out = command; \
                return true; \
            } \
            bx += width + 7; \
        } while (0)
    FILES_TRY_BAR_COMMAND(FILE_CMD_NEW_FOLDER);
    FILES_TRY_BAR_COMMAND(FILE_CMD_CUT);
    FILES_TRY_BAR_COMMAND(FILE_CMD_COPY);
    FILES_TRY_BAR_COMMAND(FILE_CMD_PASTE);
    FILES_TRY_BAR_COMMAND(FILE_CMD_RENAME);
    FILES_TRY_BAR_COMMAND(FILE_CMD_DELETE);
    FILES_TRY_BAR_COMMAND(FILE_CMD_SORT);
    FILES_TRY_BAR_COMMAND(FILE_CMD_VIEW);
    FILES_TRY_BAR_COMMAND(FILE_CMD_FILTER);
    #undef FILES_TRY_BAR_COMMAND
    return false;
}

static bool files_nav_hit(const FileExplorerLayout *layout, i32 px, i32 py, FileCommand *out) {
    i32 bx = layout->main_x + 8;
    #define FILES_TRY_NAV_COMMAND(command_value) \
        do { \
            if (point_in_rect(px, py, bx, layout->nav_y, 28, file_button_height())) { \
                *out = (command_value); \
                return true; \
            } \
            bx += 32; \
        } while (0)
    FILES_TRY_NAV_COMMAND(FILE_CMD_BACK);
    FILES_TRY_NAV_COMMAND(FILE_CMD_FORWARD);
    FILES_TRY_NAV_COMMAND(FILE_CMD_UP);
    FILES_TRY_NAV_COMMAND(FILE_CMD_REFRESH);
    #undef FILES_TRY_NAV_COMMAND
    return false;
}

static void handle_files_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 width = window->width - 12;
    i32 height = window->height - 36;
    FileExplorerLayout layout;
    files_make_layout(x, y, width, height, &layout);

    FileCommand command;
    if (files_nav_hit(&layout, mouse_x, mouse_y, &command) ||
        files_command_bar_hit(&layout, mouse_x, mouse_y, &command)) {
        files_run_command(command);
        mark_dirty_rect(window->x, window->y, window->width, window->height);
        return;
    }

    if (point_in_rect(mouse_x, mouse_y, layout.search_x, layout.nav_y, layout.search_w, 28)) {
        files_search_editing = true;
        mark_dirty_rect(window->x, window->y, window->width, window->height);
        return;
    }
    files_search_editing = false;

    i32 loc_y = layout.body_y + 4;
    for (size_t i = 0; i < file_location_count(); i++) {
        if (point_in_rect(mouse_x, mouse_y, x + 10, loc_y + (i32)i * 29, layout.sidebar_w - 18, 24)) {
            files_navigate_to_location(i, true);
            mark_dirty_rect(window->x, window->y, window->width, window->height);
            return;
        }
    }

    if (path_is_home(files_current_path)) {
        i32 card_w = layout.main_w < 520 ? (layout.main_w - 18) / 2 : (layout.main_w - 34) / 3;
        i32 card_h = 58;
        i32 grid_x = layout.main_x + 10;
        i32 grid_y = layout.body_y + 42;
        for (size_t i = 0; i < files_quick_location_count(); i++) {
            i32 col_count = layout.main_w < 520 ? 2 : 3;
            i32 col = (i32)(i % (size_t)col_count);
            i32 row = (i32)(i / (size_t)col_count);
            i32 cx = grid_x + col * (card_w + 12);
            i32 cy = grid_y + row * (card_h + 12);
            if (point_in_rect(mouse_x, mouse_y, cx, cy, card_w, card_h)) {
                files_navigate_to_location(files_quick_location(i), true);
                mark_dirty_rect(window->x, window->y, window->width, window->height);
                return;
            }
        }
    }

    files_ensure_entries();
    if (files_grid_view && !path_is_home(files_current_path)) {
        i32 card_w = layout.main_w < 520 ? (layout.main_w - 22) / 2 : (layout.main_w - 40) / 3;
        i32 card_h = 72;
        i32 grid_x = layout.main_x + 10;
        i32 grid_y = layout.body_y + 44;
        i32 col_count = layout.main_w < 520 ? 2 : 3;
        for (size_t i = 0; i < files_entry_count; i++) {
            i32 col = (i32)(i % (size_t)col_count);
            i32 row = (i32)(i / (size_t)col_count);
            i32 cx = grid_x + col * (card_w + 14);
            i32 cy = grid_y + row * (card_h + 12);
            if (point_in_rect(mouse_x, mouse_y, cx, cy, card_w, card_h)) {
                if (files_entries[i].folder) {
                    files_navigate_to(files_entries[i].path, true);
                } else {
                    files_select_index((i32)i);
                    set_taskbar_message("FILE SELECTED");
                }
                mark_dirty_rect(window->x, window->y, window->width, window->height);
                return;
            }
        }
    } else if (!path_is_home(files_current_path) &&
               point_in_rect(mouse_x, mouse_y, layout.main_x, layout.list_y, layout.main_w, layout.body_h - 104)) {
        i32 row = (mouse_y - layout.list_y) / layout.row_h;
        if (row >= 0 && row < (i32)files_entry_count) {
            if (point_in_rect(mouse_x, mouse_y, layout.main_x + 10, layout.list_y + row * layout.row_h + 9, 16, 16)) {
                files_toggle_index(row);
                set_taskbar_message("MULTI SELECT");
            } else if (files_entries[row].folder) {
                files_navigate_to(files_entries[row].path, true);
            } else {
                files_select_index(row);
                set_taskbar_message("FILE SELECTED");
            }
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

static const char *settings_page_label(i32 page) {
    if (page == 0) return "System";
    if (page == 1) return "Bluetooth & devices";
    if (page == 2) return "Network & internet";
    if (page == 3) return "Personalization";
    if (page == 4) return "Apps";
    if (page == 5) return "Accounts";
    if (page == 6) return "Time & language";
    if (page == 7) return "Accessibility";
    if (page == 8) return "Updates";
    return "About";
}

static const char *settings_page_subtitle(i32 page) {
    if (page == 0) return "Manage your device and preferences";
    if (page == 1) return "Pair devices and tune input";
    if (page == 2) return "Network status, web bridge, and downloads";
    if (page == 3) return "Wallpaper, dock, theme, and cursor";
    if (page == 4) return "Installed apps, Store, and app permissions";
    if (page == 5) return "User profile, login, and sessions";
    if (page == 6) return "Clock, region, and keyboard layout";
    if (page == 7) return "Text, motion, contrast, and input access";
    if (page == 8) return "System updates, recovery, and backups";
    return "LiquidOS device information";
}

static const char *settings_row_title(i32 page, i32 row) {
    static const char *rows[10][5] = {
        { "Display", "Sound", "Notifications", "Focus", "Power & battery" },
        { "Bluetooth", "Keyboard", "Mouse", "Touchpad", "Connected devices" },
        { "Wi-Fi", "Ethernet", "Browser networking", "Downloads", "WebBridge" },
        { "Theme", "Wallpaper", "Dock", "Window style", "Cursor" },
        { "Installed apps", "App Store", "Default apps", "Startup apps", "App permissions" },
        { "User profile", "Sign-in options", "Lock screen", "Sessions", "Family safety" },
        { "Date & time", "Region", "Keyboard layout", "Clock", "Calendar" },
        { "Text size", "Reduce motion", "Contrast", "Pointer", "Keyboard access" },
        { "Check for updates", "Update history", "Recovery", "Backup", "Developer channel" },
        { "Device info", "Storage", "Performance", "Security", "Privacy" },
    };
    return rows[page < 0 || page > 9 ? 0 : page][row < 0 || row > 4 ? 0 : row];
}

static const char *settings_row_subtitle(i32 page, i32 row) {
    if (page == 0 && row == 0) return settings_night_light ? "Night light enabled, Liquid Glass adaptive" : "Brightness, contrast, night light, display profile";
    if (page == 0 && row == 1) return settings_sound_enabled ? "System sounds enabled" : "System sounds muted";
    if (page == 0 && row == 2) return settings_notifications_enabled ? "Alerts from apps and system are enabled" : "Notifications are paused";
    if (page == 0 && row == 3) return settings_focus_enabled ? "Focus mode is on" : "Reduce distractions and stay in the zone";
    if (page == 0 && row == 4) return battery_saver ? "Battery saver enabled" : "Sleep, battery usage, battery saver";
    if (page == 1 && row == 0) return settings_bluetooth_enabled ? "Bluetooth discovery is on" : "Bluetooth discovery is off";
    if (page == 1 && row == 1) return "PS/2 keyboard active, shortcuts enabled";
    if (page == 1 && row == 2) return "Pointer acceleration and click controls";
    if (page == 1 && row == 3) return "Touchpad gestures planned";
    if (page == 1 && row == 4) return "No external devices paired";
    if (page == 2 && row == 0) return wifi_enabled ? "Wi-Fi toggle is on" : "Wi-Fi toggle is off";
    if (page == 2 && row == 1) return network_info()->link_up ? "RTL8139 network link active" : "No network link detected";
    if (page == 2 && row == 2) return "Google search and HTTP route through LiquidOS networking";
    if (page == 2 && row == 3) return "Store package downloads use LiquidFS";
    if (page == 2 && row == 4) return "Host bridge available at 10.0.2.2 when run script starts it";
    if (page == 3 && row == 0) return themes[current_theme].name;
    if (page == 3 && row == 1) return "Current purple abstract LiquidOS wallpaper";
    if (page == 3 && row == 2) return "Top dock size and Liquid Glass material";
    if (page == 3 && row == 3) return "Rounded glass frames and title controls";
    if (page == 3 && row == 4) return "Custom white pointer active";
    if (page == 4 && row == 0) return "Open Launch Apps";
    if (page == 4 && row == 1) return "Open the app installer";
    if (page == 4 && row == 2) return "Liqueia is the default browser";
    if (page == 4 && row == 3) return "Startup apps are disabled for faster boot";
    if (page == 4 && row == 4) return "Installed apps run with basic sandbox policy";
    if (page == 5 && row == 0) return "Local user session";
    if (page == 5 && row == 1) return "Password login is planned";
    if (page == 5 && row == 2) return settings_privacy_lock ? "Lock screen privacy enabled" : "Lock screen privacy disabled";
    if (page == 5 && row == 3) return "Single desktop session active";
    if (page == 5 && row == 4) return "Family controls are planned";
    if (page == 6 && row == 0) return "Synced from VM clock using UK timezone";
    if (page == 6 && row == 1) return "United Kingdom";
    if (page == 6 && row == 2) return "UK keyboard layout";
    if (page == 6 && row == 3) return "24-hour clock shown in dock";
    if (page == 6 && row == 4) return "Calendar services planned";
    if (page == 7 && row == 0) return settings_large_text ? "Large text enabled" : "Default text size";
    if (page == 7 && row == 1) return settings_reduce_motion ? "Motion reduced" : "Smooth dock and window movement";
    if (page == 7 && row == 2) return settings_high_contrast ? "High contrast enabled" : "Liquid Glass contrast";
    if (page == 7 && row == 3) return "Cursor shape and size";
    if (page == 7 && row == 4) return "Keyboard navigation and shortcuts";
    if (page == 8 && row == 0) return "LiquidOS is up to date";
    if (page == 8 && row == 1) return "Last local build installed";
    if (page == 8 && row == 2) return "Safe mode and recovery planned";
    if (page == 8 && row == 3) return "Backups planned for persistent disks";
    if (page == 8 && row == 4) return "Developer preview channel";
    if (page == 9 && row == 0) return "Aurora, LiquidOS Developer Preview";
    if (page == 9 && row == 1) return fs_persistence_available() ? "LiquidFS disk-backed" : "LiquidFS RAM only";
    if (page == 9 && row == 2) return "Dirty-region rendering and coalesced input";
    if (page == 9 && row == 3) return "User/kernel boundary and syscall policy improving";
    return "Privacy controls and sandboxing roadmap";
}

static void settings_cycle_theme(void) {
    current_theme = (current_theme + 1) % (sizeof(themes) / sizeof(themes[0]));
    char theme_id[2];
    theme_id[0] = (char)('0' + current_theme);
    theme_id[1] = 0;
    fs_write("SYSTEM/THEME.TXT", theme_id);
    set_taskbar_message(themes[current_theme].name);
    push_notification("Settings", "Theme changed.");
    mark_dirty_full();
}

static void settings_handle_row(i32 page, i32 row) {
    if (page == 0 && row == 0) { settings_night_light = !settings_night_light; set_taskbar_message(settings_night_light ? "NIGHT LIGHT" : "DISPLAY"); return; }
    if (page == 0 && row == 1) { settings_sound_enabled = !settings_sound_enabled; set_taskbar_message(settings_sound_enabled ? "SOUND ON" : "MUTED"); return; }
    if (page == 0 && row == 2) { settings_notifications_enabled = !settings_notifications_enabled; set_taskbar_message(settings_notifications_enabled ? "NOTIFY ON" : "NOTIFY OFF"); if (settings_notifications_enabled) push_notification("Settings", "Notifications are enabled."); return; }
    if (page == 0 && row == 3) { settings_focus_enabled = !settings_focus_enabled; set_taskbar_message(settings_focus_enabled ? "FOCUS ON" : "FOCUS OFF"); return; }
    if (page == 0 && row == 4) { battery_saver = !battery_saver; set_taskbar_message(battery_saver ? "BATTERY SAVER" : "FULL POWER"); return; }
    if (page == 1 && row == 0) { settings_bluetooth_enabled = !settings_bluetooth_enabled; set_taskbar_message(settings_bluetooth_enabled ? "BT ON" : "BT OFF"); return; }
    if (page == 2 && row == 0) { wifi_enabled = !wifi_enabled; set_taskbar_message(wifi_enabled ? "WIFI ON" : "WIFI OFF"); return; }
    if (page == 2 && row == 2) { open_window(WINDOW_BROWSER); set_taskbar_message("BROWSER"); return; }
    if (page == 2 && row == 3) { open_window(WINDOW_STORE); set_taskbar_message("STORE"); return; }
    if (page == 3 && row == 0) { settings_cycle_theme(); return; }
    if (page == 3 && row == 2) { dock_scale_percent += 8; if (dock_scale_percent > 135) dock_scale_percent = 82; mark_dirty_taskbar(); set_taskbar_message("DOCK SIZE"); return; }
    if (page == 4 && row == 0) { open_window(WINDOW_LAUNCHER); set_taskbar_message("APPS"); return; }
    if (page == 4 && row == 1) { open_window(WINDOW_STORE); set_taskbar_message("STORE"); return; }
    if (page == 5 && row == 2) { settings_privacy_lock = !settings_privacy_lock; set_taskbar_message(settings_privacy_lock ? "LOCK PRIVACY" : "LOCK OPEN"); return; }
    if (page == 7 && row == 0) { settings_large_text = !settings_large_text; set_taskbar_message(settings_large_text ? "LARGE TEXT" : "TEXT SIZE"); return; }
    if (page == 7 && row == 1) { settings_reduce_motion = !settings_reduce_motion; set_taskbar_message(settings_reduce_motion ? "MOTION LESS" : "MOTION ON"); return; }
    if (page == 7 && row == 2) { settings_high_contrast = !settings_high_contrast; set_taskbar_message(settings_high_contrast ? "CONTRAST" : "GLASS"); return; }
    if (page == 8 && row == 0) { set_taskbar_message("UP TO DATE"); push_notification("Updates", "LiquidOS is up to date."); return; }
    if (page == 9 && row == 1) { open_window(WINDOW_FILES); set_taskbar_message("STORAGE"); return; }
    set_taskbar_message(settings_row_title(page, row));
}

static void handle_settings_click(const Window *window) {
    i32 x = window->x + 6;
    i32 y = window->y + 30;
    i32 width = window->width - 12;
    i32 sidebar_w = width < 760 ? 218 : 258;
    i32 nav_w = sidebar_w - 36;

    if (point_in_rect(mouse_x, mouse_y, x + 22, y + 70, nav_w, 40)) {
        settings_search_editing = true;
        mark_dirty_rect(window->x, window->y, window->width, window->height);
        return;
    }
    settings_search_editing = false;

    i32 nav_y = y + 122;
    for (i32 i = 0; i < 10; i++) {
        if (point_in_rect(mouse_x, mouse_y, x + 18, nav_y + i * 42, nav_w, 34)) {
            settings_page = i;
            set_taskbar_message(settings_page_label(i));
            mark_dirty_rect(window->x, window->y, window->width, window->height);
            return;
        }
    }

    i32 content_x = x + sidebar_w + 36;
    i32 row_y = y + 244;
    i32 row_w = width - sidebar_w - 70;
    for (i32 i = 0; i < 5; i++) {
        if (point_in_rect(mouse_x, mouse_y, content_x, row_y + i * 68, row_w, 62)) {
            settings_handle_row(settings_page, i);
            mark_dirty_rect(window->x, window->y, window->width, window->height);
            return;
        }
    }
}

static void settings_on_char(char ch) {
    if (!settings_search_editing) {
        return;
    }
    size_t length = strlen(settings_search);
    if (ch == 8) {
        if (length > 0) {
            settings_search[length - 1] = 0;
        }
    } else if (ch >= 32 && ch <= 126 && length + 1 < sizeof(settings_search)) {
        settings_search[length] = ch;
        settings_search[length + 1] = 0;
    }
    for (i32 i = 0; settings_search[0] && i < 10; i++) {
        if (contains_text_ci(settings_page_label(i), settings_search)) {
            settings_page = i;
            break;
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
                files_mark_dirty();
                set_taskbar_message("FILE CREATED");
                push_notification("Desktop", "Created a new desktop file.");
            }
        }
    }
    context_menu_open = false;
    mark_dirty_full();
}

static Window content_click_window(const Window *window) {
    Window content_window = *window;
    if (window->expanded) {
        content_window.x = window->x - 6;
        content_window.y = window->y - 30;
        content_window.width = window->width + 12;
        content_window.height = window->height + 36;
    }
    return content_window;
}

static void dispatch_window_content_click(WindowKind kind, const Window *window) {
    Window content_window = content_click_window(window);
    const Window *target = &content_window;
    if (kind == WINDOW_FILES) {
        handle_files_click(target);
    } else if (kind == WINDOW_BROWSER) {
        handle_browser_click(target);
    } else if (kind == WINDOW_STORE) {
        handle_store_click(target);
    } else if (kind == WINDOW_SETTINGS) {
        handle_settings_click(target);
    } else if (kind == WINDOW_LAUNCHER) {
        handle_launcher_click(target);
    } else if (kind == WINDOW_APP_VIEW) {
        handle_app_view_click(target);
    } else if (kind == WINDOW_CONTROL_CENTER) {
        handle_control_center_click(target);
    }
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

        if (window->expanded && !fullscreen_chrome_visible) {
            dispatch_window_content_click(kind, window);
            return;
        }

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

        if (window->expanded) {
            dispatch_window_content_click(kind, window);
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
            dispatch_window_content_click(kind, window);
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

static void draw_liquid_reference_glass(i32 x, i32 y, i32 width, i32 height, i32 radius, bool shadow) {
    if (shadow) {
        gfx_fill_round_rect_alpha(x, y + 5, width, height, radius, RGB(0, 0, 0), 38);
    }
    i32 inner_radius = radius > 2 ? radius - 2 : radius;
    gfx_liquid_glass_rect(x, y, width, height, radius);
    gfx_draw_round_rect_alpha(x, y, width, height, radius, RGB(246, 252, 255), 64);
    gfx_draw_round_rect_alpha(x + 1, y + 1, width - 2, height - 2, inner_radius, RGB(255, 255, 255), 18);
}

static void draw_glass_panel(i32 x, i32 y, i32 width, i32 height, i32 radius) {
    draw_liquid_reference_glass(x, y, width, height, radius, true);
}

static void draw_background(void) {
    gfx_draw_wallpaper();
}

static void draw_dock_glass_capsule(i32 x, i32 y, i32 width, i32 height, i32 radius) {
    gfx_liquid_filter_glass_rect(x, y, width, height, radius);
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

static bool ui_text_pointer_valid(const char *text) {
    uintptr_t value = (uintptr_t)text;
    return value >= 0x1000 && value < 0x40000000ULL;
}

static const char *window_safe_title(const Window *window) {
    return (window && ui_text_pointer_valid(window->title)) ? window->title : "Window";
}

static i32 window_title_chip_width(const Window *window) {
    i32 width = 78 + (i32)strlen(window_safe_title(window)) * 6;
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

static void draw_settings_dock_glyph(i32 x, i32 y, i32 tile_size) {
    i32 cx = x + tile_size / 2;
    i32 cy = y + tile_size / 2;
    i32 tooth = tile_size / 8;
    i32 outer = tile_size / 4;
    i32 inner = tile_size / 9;
    Color white = RGB(248, 252, 255);
    Color core = RGB(84, 104, 128);

    gfx_fill_round_rect_alpha(cx - tooth / 2, cy - outer - tooth, tooth, tooth + 4, tooth / 2, white, 220);
    gfx_fill_round_rect_alpha(cx - tooth / 2, cy + outer - 4, tooth, tooth + 4, tooth / 2, white, 220);
    gfx_fill_round_rect_alpha(cx - outer - tooth, cy - tooth / 2, tooth + 4, tooth, tooth / 2, white, 220);
    gfx_fill_round_rect_alpha(cx + outer - 4, cy - tooth / 2, tooth + 4, tooth, tooth / 2, white, 220);
    gfx_fill_round_rect_alpha(cx - outer + 1, cy - outer + 1, tooth + 2, tooth + 2, tooth / 2, white, 190);
    gfx_fill_round_rect_alpha(cx + outer - tooth - 3, cy - outer + 1, tooth + 2, tooth + 2, tooth / 2, white, 190);
    gfx_fill_round_rect_alpha(cx - outer + 1, cy + outer - tooth - 3, tooth + 2, tooth + 2, tooth / 2, white, 190);
    gfx_fill_round_rect_alpha(cx + outer - tooth - 3, cy + outer - tooth - 3, tooth + 2, tooth + 2, tooth / 2, white, 190);
    gfx_fill_circle_alpha(cx, cy, outer, white, 224);
    gfx_fill_circle_alpha(cx, cy, inner + 4, core, 235);
    gfx_fill_circle_alpha(cx, cy, inner, RGB(18, 24, 34), 120);
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

static void draw_dock_resize_grip(const TaskbarLayout *bar) {
    i32 grip_x;
    i32 grip_y;
    i32 grip_size;
    dock_grip_rect(bar, &grip_x, &grip_y, &grip_size);
    i32 dock_radius = dock_reference_radius();
    i32 grip_w = grip_size + dock_scale_value(9);
    i32 grip_h = grip_size;
    i32 grip_radius = dock_child_radius(grip_h, bar->h, dock_radius);
    i32 draw_x = bar->x + bar->w - grip_w - dock_scale_value(2);
    i32 draw_y = bar->y + bar->h - grip_h - dock_scale_value(2);
    (void)grip_x;
    (void)grip_y;
    gfx_liquid_glass_grip(draw_x, draw_y, grip_w, grip_h, grip_radius);
}

static void draw_window_chrome_group(const Window *window, bool focused) {
    const DesktopTheme *theme = &themes[current_theme];
    i32 title_x;
    i32 title_w;
    i32 min_x;
    i32 max_x;
    i32 close_x;
    i32 control_y;
    i32 control_size;
    window_title_group_layout(window, &title_x, &title_w, &min_x, &max_x, &close_x, &control_y, &control_size);
    i32 chip_radius = (control_size * 24) / (window->height > 0 ? window->height : 1);
    if (chip_radius < 8) {
        chip_radius = 8;
    }

    draw_liquid_reference_glass(title_x, control_y, title_w, control_size, chip_radius, false);
    gfx_fill_round_rect_alpha(title_x + 7, control_y + 6, 12, 12, 5, theme->accent, focused ? 156 : 92);
    gfx_draw_text(title_x + 26, control_y + 7, window_safe_title(window), focused ? theme->text : RGB(132, 142, 154), 1);

    draw_liquid_reference_glass(min_x, control_y, control_size, control_size, chip_radius, false);
    gfx_fill_round_rect_plain_alpha(min_x, control_y, control_size, control_size, chip_radius, RGB(242, 190, 76), focused ? 34 : 18);
    gfx_fill_rect(min_x + 7, control_y + 13, control_size - 14, 2, RGB(255, 246, 214));

    draw_liquid_reference_glass(max_x, control_y, control_size, control_size, chip_radius, false);
    gfx_fill_round_rect_plain_alpha(max_x, control_y, control_size, control_size, chip_radius, RGB(83, 196, 101), focused ? 32 : 16);
    gfx_draw_rect(max_x + 7, control_y + 7, control_size - 14, control_size - 14, RGB(224, 255, 230));

    draw_liquid_reference_glass(close_x, control_y, control_size, control_size, chip_radius, false);
    gfx_fill_round_rect_plain_alpha(close_x, control_y, control_size, control_size, chip_radius, RGB(238, 94, 88), focused ? 32 : 16);
    gfx_draw_line(close_x + 8, control_y + 8, close_x + control_size - 8, control_y + control_size - 8, RGB(255, 226, 224));
    gfx_draw_line(close_x + control_size - 8, control_y + 8, close_x + 8, control_y + control_size - 8, RGB(255, 226, 224));
}

static void draw_window_frame(const Window *window, bool focused) {
    const DesktopTheme *theme = &themes[current_theme];
    i32 radius = 24;
    Color rim = RGB(94, 94, 94);
    bool settings_window = window == &windows[WINDOW_SETTINGS];

    draw_liquid_reference_glass(window->x, window->y, window->width, window->height, radius, true);
    gfx_fill_round_rect_alpha(window->x + 6, window->y + 30, window->width - 12, window->height - 36,
                              radius - 8,
                              settings_window ? RGB(248, 250, 255) : theme->panel,
                              settings_window ? (focused ? 16 : 10) : (focused ? 220 : 196));
    gfx_draw_round_rect_alpha(window->x, window->y, window->width, window->height, radius,
                              rim, focused ? 255 : 210);
    draw_window_chrome_group(window, focused);

    gfx_draw_line(window->x + window->width - 17, window->y + window->height - 6, window->x + window->width - 6, window->y + window->height - 17, RGB(132, 146, 154));
    gfx_draw_line(window->x + window->width - 12, window->y + window->height - 5, window->x + window->width - 5, window->y + window->height - 12, RGB(132, 146, 154));
}

static i32 file_button_height(void) {
    return 24;
}

static void draw_file_button(i32 x, i32 y, i32 width, const char *label, bool active) {
    i32 height = file_button_height();
    i32 radius = 8;
    gfx_blur_round_rect(x, y, width, height, radius);
    gfx_refract_round_rect_edges(x, y, width, height, radius, 1);
    gfx_fill_round_rect_plain_alpha(x, y, width, height, radius,
                                    active ? themes[current_theme].accent : RGB(246, 250, 255),
                                    active ? 88 : 58);
    gfx_draw_round_rect_alpha(x, y, width, height, radius,
                              active ? RGB(246, 252, 255) : RGB(162, 178, 194),
                              active ? 54 : 32);
    i32 text_x = x + 9;
    if (width <= 32) {
        text_x = x + width / 2 - 4;
    }
    gfx_draw_text(text_x, y + 8, label, active ? RGB(255, 255, 255) : RGB(48, 58, 68), 1);
}

static void draw_file_command_button(FileCommand command, i32 x, i32 y, i32 width) {
    if (command == FILE_CMD_BACK) { draw_file_button(x, y, width, "<", false); return; }
    if (command == FILE_CMD_FORWARD) { draw_file_button(x, y, width, ">", false); return; }
    if (command == FILE_CMD_UP) { draw_file_button(x, y, width, "^", false); return; }
    if (command == FILE_CMD_NEW_FOLDER) { draw_file_button(x, y, width, "New", false); return; }
    if (command == FILE_CMD_COPY) { draw_file_button(x, y, width, "Copy", false); return; }
    if (command == FILE_CMD_CUT) { draw_file_button(x, y, width, "Cut", false); return; }
    if (command == FILE_CMD_PASTE) { draw_file_button(x, y, width, "Paste", false); return; }
    if (command == FILE_CMD_RENAME) { draw_file_button(x, y, width, "Rename", false); return; }
    if (command == FILE_CMD_DELETE) { draw_file_button(x, y, width, "Trash", false); return; }
    if (command == FILE_CMD_REFRESH) { draw_file_button(x, y, width, "R", false); return; }
    if (command == FILE_CMD_SORT) { draw_file_button(x, y, width, files_sort_desc ? "Z-A" : "A-Z", files_sort_desc); return; }
    if (command == FILE_CMD_VIEW) { draw_file_button(x, y, width, files_grid_view ? "Grid" : "List", files_grid_view); return; }
    if (command == FILE_CMD_FILTER) { draw_file_button(x, y, width, files_filter_folders ? "Folders" : "Filter", files_filter_folders); return; }
    if (command == FILE_CMD_DETAILS) { draw_file_button(x, y, width, "Details", files_details_visible); return; }
}

static void draw_file_location_label(size_t index, i32 x, i32 y, Color color) {
    if (index == 0) { gfx_draw_text(x, y, "Home", color, 1); return; }
    if (index == 1) { gfx_draw_text(x, y, "Desktop", color, 1); return; }
    if (index == 2) { gfx_draw_text(x, y, "Documents", color, 1); return; }
    if (index == 3) { gfx_draw_text(x, y, "Downloads", color, 1); return; }
    if (index == 4) { gfx_draw_text(x, y, "Pictures", color, 1); return; }
    if (index == 5) { gfx_draw_text(x, y, "Music", color, 1); return; }
    if (index == 6) { gfx_draw_text(x, y, "Videos", color, 1); return; }
    if (index == 7) { gfx_draw_text(x, y, "Applications", color, 1); return; }
    if (index == 8) { gfx_draw_text(x, y, "System", color, 1); return; }
    if (index == 9) { gfx_draw_text(x, y, "Trash", color, 1); return; }
}

static Color file_location_color(size_t index) {
    if (index == 1) return RGB(38, 155, 220);
    if (index == 2) return RGB(116, 142, 170);
    if (index == 3) return RGB(26, 180, 124);
    if (index == 4) return RGB(56, 152, 226);
    if (index == 5) return RGB(218, 104, 96);
    if (index == 6) return RGB(154, 82, 218);
    if (index == 7) return RGB(210, 154, 64);
    if (index == 8) return RGB(92, 108, 126);
    if (index == 9) return RGB(132, 140, 150);
    return RGB(92, 148, 220);
}

static void draw_location_icon(size_t index, i32 x, i32 y, i32 size, bool active) {
    Color color = file_location_color(index);
    gfx_fill_round_rect_alpha(x, y, size, size, size / 4, color, active ? 235 : 205);
    gfx_fill_round_rect_alpha(x + size / 8, y + size / 8, size - size / 4, size / 3, size / 7,
                              RGB(255, 255, 255), active ? 64 : 42);
    if (index == 0) {
        gfx_draw_line(x + size / 4, y + size / 2, x + size / 2, y + size / 4, RGB(255, 255, 255));
        gfx_draw_line(x + size / 2, y + size / 4, x + (size * 3) / 4, y + size / 2, RGB(255, 255, 255));
    } else if (index == 3) {
        gfx_fill_rect(x + size / 2 - 1, y + size / 4, 2, size / 2, RGB(255, 255, 255));
        gfx_draw_line(x + size / 3, y + size / 2, x + size / 2, y + (size * 2) / 3, RGB(255, 255, 255));
        gfx_draw_line(x + size / 2, y + (size * 2) / 3, x + (size * 2) / 3, y + size / 2, RGB(255, 255, 255));
    } else if (index == 9) {
        gfx_draw_rect(x + size / 3, y + size / 3, size / 3, size / 2, RGB(255, 255, 255));
        gfx_fill_rect(x + size / 3 - 1, y + size / 4, size / 3 + 2, 2, RGB(255, 255, 255));
    }
    gfx_draw_round_rect_alpha(x, y, size, size, size / 4, RGB(255, 255, 255), 54);
}

static void draw_text_trimmed(i32 x, i32 y, const char *text, size_t max_chars, Color color) {
    char out[96];
    size_t used = 0;
    while (text[used] && used < max_chars && used + 1 < sizeof(out)) {
        out[used] = text[used];
        used++;
    }
    if (text[used] && used > 3) {
        out[used - 3] = '.';
        out[used - 2] = '.';
        out[used - 1] = '.';
    }
    out[used] = 0;
    gfx_draw_text(x, y, out, color, 1);
}

static void format_size_text(u64 size, bool folder, char *out, size_t out_size) {
    if (folder) {
        strncpy(out, "--", out_size - 1);
        out[out_size - 1] = 0;
        return;
    }
    char number[24];
    u64_to_dec(size, number, sizeof(number));
    out[0] = 0;
    append_text(out, out_size, number);
    append_text(out, out_size, " B");
}

static void format_modified_text(u64 tick, char *out, size_t out_size) {
    char number[24];
    u64_to_dec(tick, number, sizeof(number));
    out[0] = 0;
    append_text(out, out_size, "T+");
    append_text(out, out_size, number);
}

static void draw_file_icon(i32 x, i32 y, bool folder, bool selected) {
    if (folder) {
        gfx_fill_round_rect_alpha(x, y + 4, 25, 20, 7, RGB(235, 190, 78), selected ? 245 : 220);
        gfx_fill_round_rect_alpha(x + 3, y, 12, 9, 4, RGB(252, 218, 115), selected ? 245 : 220);
        gfx_fill_round_rect_alpha(x + 2, y + 8, 28, 20, 7, RGB(246, 204, 83), selected ? 245 : 230);
        gfx_draw_round_rect_alpha(x + 2, y + 8, 28, 20, 7, RGB(255, 248, 210), 70);
    } else {
        gfx_fill_round_rect_alpha(x + 3, y, 24, 30, 7, RGB(232, 244, 255), selected ? 245 : 225);
        gfx_fill_rect(x + 8, y + 9, 14, 1, RGB(120, 152, 184));
        gfx_fill_rect(x + 8, y + 15, 12, 1, RGB(120, 152, 184));
        gfx_fill_rect(x + 8, y + 21, 15, 1, RGB(120, 152, 184));
        gfx_draw_round_rect_alpha(x + 3, y, 24, 30, 7, RGB(255, 255, 255), 70);
    }
}

static void draw_file_explorer(i32 x, i32 y, i32 width, i32 height) {
    const DesktopTheme *theme = &themes[current_theme];
    FileExplorerLayout layout;

    files_ensure_entries();
    files_make_layout(x, y, width, height, &layout);

    gfx_blur_round_rect(x, y, width, height, 20);
    gfx_refract_round_rect_edges(x, y, width, height, 20, 1);
    gfx_fill_round_rect_plain_alpha(x, y, width, height, 20, RGB(246, 250, 253), 224);
    gfx_fill_round_rect_plain_alpha(x + 1, y + 1, width - 2, 40, 19, RGB(226, 238, 248), 168);
    gfx_draw_round_rect_alpha(x, y, width, height, 20, RGB(246, 252, 255), 58);

    i32 nav_x = x + 14;
    draw_file_command_button(FILE_CMD_BACK, nav_x, layout.nav_y, 28); nav_x += 32;
    draw_file_command_button(FILE_CMD_FORWARD, nav_x, layout.nav_y, 28); nav_x += 32;
    draw_file_command_button(FILE_CMD_UP, nav_x, layout.nav_y, 28); nav_x += 32;
    draw_file_command_button(FILE_CMD_REFRESH, nav_x, layout.nav_y, 28);

    gfx_blur_round_rect(layout.address_x, layout.nav_y, layout.address_w, 26, 10);
    gfx_refract_round_rect_edges(layout.address_x, layout.nav_y, layout.address_w, 26, 10, 1);
    gfx_fill_round_rect_plain_alpha(layout.address_x, layout.nav_y, layout.address_w, 26, 10, RGB(255, 255, 255), 178);
    gfx_draw_round_rect_alpha(layout.address_x, layout.nav_y, layout.address_w, 26, 10, RGB(184, 202, 218), 42);
    if (path_is_home(files_current_path)) {
        gfx_draw_text(layout.address_x + 12, layout.nav_y + 9, "Home", theme->text, 1);
    } else {
        gfx_draw_text(layout.address_x + 12, layout.nav_y + 9, "Home", RGB(76, 88, 100), 1);
        gfx_draw_text(layout.address_x + 48, layout.nav_y + 9, ">", RGB(112, 124, 136), 1);
        draw_text_trimmed(layout.address_x + 66, layout.nav_y + 9, files_current_path[0] ? files_current_path : "LiquidOS", 28, theme->text);
    }

    gfx_blur_round_rect(layout.search_x, layout.nav_y, layout.search_w, 26, 10);
    gfx_refract_round_rect_edges(layout.search_x, layout.nav_y, layout.search_w, 26, 10, 1);
    gfx_fill_round_rect_plain_alpha(layout.search_x, layout.nav_y, layout.search_w, 26, 10, RGB(255, 255, 255), files_search_editing ? 214 : 178);
    gfx_draw_round_rect_alpha(layout.search_x, layout.nav_y, layout.search_w, 26, 10, RGB(184, 202, 218), files_search_editing ? 78 : 42);
    gfx_draw_text(layout.search_x + 12, layout.nav_y + 9, files_search[0] ? files_search : "Search", files_search[0] ? theme->text : RGB(112, 124, 136), 1);

    gfx_fill_round_rect_plain_alpha(x + 8, layout.command_y - 5, width - 16, 34, 12, RGB(255, 255, 255), 116);
    gfx_draw_round_rect_alpha(x + 8, layout.command_y - 5, width - 16, 34, 12, RGB(176, 194, 210), 24);
    i32 bx = x + 12;
    i32 details_w = file_command_width(FILE_CMD_DETAILS);
    i32 details_x = x + width - details_w - 14;
    #define DRAW_FILE_BAR_COMMAND(command_value) \
        do { \
            FileCommand command = (command_value); \
            i32 button_w = file_command_width(command); \
            if (bx + button_w <= details_x - 12) { \
                draw_file_command_button(command, bx, layout.command_y, button_w); \
                bx += button_w + 7; \
            } \
        } while (0)
    DRAW_FILE_BAR_COMMAND(FILE_CMD_NEW_FOLDER);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_CUT);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_COPY);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_PASTE);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_RENAME);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_DELETE);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_SORT);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_VIEW);
    DRAW_FILE_BAR_COMMAND(FILE_CMD_FILTER);
    #undef DRAW_FILE_BAR_COMMAND
    draw_file_command_button(FILE_CMD_DETAILS, details_x, layout.command_y, details_w);

    gfx_fill_round_rect_plain_alpha(x + 8, layout.body_y - 2, layout.sidebar_w - 16, layout.body_h + 8, 16, RGB(247, 250, 253), 178);
    gfx_draw_round_rect_alpha(x + 8, layout.body_y - 2, layout.sidebar_w - 16, layout.body_h + 8, 16, RGB(198, 214, 228), 32);
    gfx_draw_text(x + 22, layout.body_y + 10, "Home", theme->text, 1);
    i32 loc_y = layout.body_y + 32;
    for (size_t i = 0; i < file_location_count(); i++) {
        bool active = file_location_matches(i, files_current_path);
        gfx_fill_round_rect_plain_alpha(x + 10, loc_y, layout.sidebar_w - 18, 24, 7,
                                        active ? RGB(220, 234, 250) : RGB(255, 255, 255),
                                        active ? 220 : 42);
        draw_location_icon(i, x + 18, loc_y + 5, 14, active);
        draw_file_location_label(i, x + 38, loc_y + 8, active ? RGB(34, 58, 92) : RGB(48, 58, 68));
        loc_y += 29;
    }

    gfx_fill_round_rect_plain_alpha(layout.main_x, layout.body_y - 2, layout.main_w, layout.body_h + 8, 18, RGB(255, 255, 255), 202);
    gfx_draw_round_rect_alpha(layout.main_x, layout.body_y - 2, layout.main_w, layout.body_h + 8, 18, RGB(206, 220, 232), 28);

    if (path_is_home(files_current_path)) {
        gfx_draw_text(layout.main_x + 12, layout.body_y + 14, "Quick access", theme->text, 1);
        i32 card_w = layout.main_w < 520 ? (layout.main_w - 18) / 2 : (layout.main_w - 34) / 3;
        i32 card_h = 58;
        i32 grid_x = layout.main_x + 10;
        i32 grid_y = layout.body_y + 42;
        i32 col_count = layout.main_w < 520 ? 2 : 3;
        for (size_t i = 0; i < files_quick_location_count(); i++) {
            size_t location = files_quick_location(i);
            i32 col = (i32)(i % (size_t)col_count);
            i32 row = (i32)(i / (size_t)col_count);
            i32 cx = grid_x + col * (card_w + 12);
            i32 cy = grid_y + row * (card_h + 12);
            gfx_blur_round_rect(cx, cy, card_w, card_h, 12);
            gfx_refract_round_rect_edges(cx, cy, card_w, card_h, 12, 1);
            gfx_fill_round_rect_plain_alpha(cx, cy, card_w, card_h, 12, RGB(248, 252, 255), 156);
            gfx_draw_round_rect_alpha(cx, cy, card_w, card_h, 12, RGB(184, 204, 220), 38);
            draw_location_icon(location, cx + 14, cy + 13, 30, false);
            draw_file_location_label(location, cx + 54, cy + 12, theme->text);
            gfx_draw_text(cx + 54, cy + 30, "Stored locally", RGB(92, 104, 116), 1);
            gfx_draw_text(cx + card_w - 20, cy + 30, "*", RGB(122, 134, 146), 1);
        }

        i32 recent_y = grid_y + ((files_quick_location_count() + (size_t)col_count - 1) / (size_t)col_count) * (card_h + 12) + 12;
        gfx_draw_text(layout.main_x + 12, recent_y + 8, "Recent activity", theme->text, 1);
        gfx_draw_text(layout.main_x + 126, recent_y + 8, "Favorites and shared views coming later", RGB(92, 104, 116), 1);

        i32 empty_y = recent_y + 44;
        i32 max_empty_y = y + height - 128;
        if (empty_y > max_empty_y) {
            empty_y = max_empty_y;
        }
        gfx_fill_circle_alpha(layout.main_x + layout.main_w / 2, empty_y + 28, 26, theme->accent, 190);
        gfx_draw_text(layout.main_x + layout.main_w / 2 - 5, empty_y + 18, "!", RGB(255, 255, 255), 2);
        gfx_draw_text(layout.main_x + layout.main_w / 2 - 78, empty_y + 68, "Your recent activity will show here", theme->text, 1);
        gfx_draw_text(layout.main_x + layout.main_w / 2 - 118, empty_y + 86, "You'll get quick access to recently opened files here.", RGB(92, 104, 116), 1);
    } else {
        gfx_draw_text(layout.main_x + 12, layout.body_y + 14, files_grid_view ? "Files and folders" : "Name", RGB(88, 98, 108), 1);
        if (!files_grid_view) {
            gfx_draw_text(layout.main_x + layout.main_w - 202, layout.body_y + 14, "Type", RGB(88, 98, 108), 1);
            gfx_draw_text(layout.main_x + layout.main_w - 132, layout.body_y + 14, "Size", RGB(88, 98, 108), 1);
            gfx_draw_text(layout.main_x + layout.main_w - 74, layout.body_y + 14, "Modified", RGB(88, 98, 108), 1);
        }

        if (files_grid_view) {
            i32 card_w = layout.main_w < 520 ? (layout.main_w - 22) / 2 : (layout.main_w - 40) / 3;
            i32 card_h = 72;
            i32 grid_x = layout.main_x + 10;
            i32 grid_y = layout.body_y + 44;
            i32 col_count = layout.main_w < 520 ? 2 : 3;
            for (size_t i = 0; i < files_entry_count; i++) {
                FileExplorerEntry *entry = &files_entries[i];
                bool selected = entry->selected || (i32)i == selected_file_index;
                i32 col = (i32)(i % (size_t)col_count);
                i32 row = (i32)(i / (size_t)col_count);
                i32 cx = grid_x + col * (card_w + 14);
                i32 cy = grid_y + row * (card_h + 12);
                if (cy + card_h > y + height - 14) {
                    break;
                }
                gfx_blur_round_rect(cx, cy, card_w, card_h, 12);
                gfx_refract_round_rect_edges(cx, cy, card_w, card_h, 12, 1);
                gfx_fill_round_rect_plain_alpha(cx, cy, card_w, card_h, 12,
                                                selected ? RGB(220, 236, 255) : RGB(248, 252, 255), selected ? 210 : 156);
                gfx_draw_round_rect_alpha(cx, cy, card_w, card_h, 12, RGB(184, 204, 220), selected ? 58 : 32);
                draw_file_icon(cx + 14, cy + 20, entry->folder, selected);
                draw_text_trimmed(cx + 54, cy + 18, entry->name, (size_t)((card_w - 66) / 7), selected ? RGB(22, 55, 94) : theme->text);
                gfx_draw_text(cx + 54, cy + 38, file_type_for_name(entry->name, entry->folder), RGB(92, 104, 116), 1);
            }
        } else {
            size_t visible_rows = (size_t)((layout.body_h - 104) / layout.row_h);
            if (visible_rows > files_entry_count) {
                visible_rows = files_entry_count;
            }
            for (size_t i = 0; i < visible_rows; i++) {
                FileExplorerEntry *entry = &files_entries[i];
                bool selected = entry->selected || (i32)i == selected_file_index;
                i32 ry = layout.list_y + (i32)i * layout.row_h;
                gfx_fill_round_rect_plain_alpha(layout.main_x + 8, ry, layout.main_w - 16, layout.row_h - 4, 8,
                                                selected ? RGB(220, 236, 255) : RGB(255, 255, 255), selected ? 218 : 52);
                gfx_draw_round_rect_alpha(layout.main_x + 10, ry + 9, 16, 16, 5, selected ? theme->accent : RGB(170, 182, 194), 112);
                if (entry->selected) {
                    gfx_fill_round_rect_alpha(layout.main_x + 13, ry + 12, 10, 10, 4, theme->accent, 220);
                }
                draw_file_icon(layout.main_x + 34, ry + 4, entry->folder, selected);
                size_t name_chars = layout.main_w > 330 ? (size_t)((layout.main_w - 300) / 7) : 10;
                draw_text_trimmed(layout.main_x + 72, ry + 10, entry->name, name_chars, selected ? RGB(22, 55, 94) : theme->text);
                gfx_draw_text(layout.main_x + layout.main_w - 202, ry + 10, file_type_for_name(entry->name, entry->folder), RGB(88, 98, 108), 1);
                char size_text[24];
                char modified_text[24];
                format_size_text(entry->size, entry->folder, size_text, sizeof(size_text));
                format_modified_text(entry->modified_tick, modified_text, sizeof(modified_text));
                gfx_draw_text(layout.main_x + layout.main_w - 132, ry + 10, size_text, RGB(88, 98, 108), 1);
                gfx_draw_text(layout.main_x + layout.main_w - 74, ry + 10, modified_text, RGB(88, 98, 108), 1);
            }
        }

        if (files_entry_count == 0) {
            gfx_draw_text(layout.main_x + 18, layout.list_y + 16, "This folder is empty.", RGB(94, 106, 118), 1);
        }
    }

    if (layout.preview_w > 0) {
        i32 px = layout.preview_x;
        gfx_blur_round_rect(px, layout.body_y - 2, layout.preview_w, layout.body_h + 8, 18);
        gfx_refract_round_rect_edges(px, layout.body_y - 2, layout.preview_w, layout.body_h + 8, 18, 1);
        gfx_fill_round_rect_plain_alpha(px, layout.body_y - 2, layout.preview_w, layout.body_h + 8, 18, RGB(248, 252, 255), 190);
        gfx_draw_round_rect_alpha(px, layout.body_y - 2, layout.preview_w, layout.body_h + 8, 18, RGB(194, 212, 226), 34);
        FileExplorerEntry *selected = files_primary_entry();
        gfx_draw_text(px + 14, layout.body_y + 16, "Details", theme->text, 1);
        if (selected) {
            draw_file_icon(px + 16, layout.body_y + 44, selected->folder, true);
            draw_text_trimmed(px + 54, layout.body_y + 50, selected->name, 17, theme->text);
            gfx_draw_text(px + 16, layout.body_y + 92, "Type", RGB(100, 112, 124), 1);
            gfx_draw_text(px + 74, layout.body_y + 92, file_type_for_name(selected->name, selected->folder), theme->text, 1);
            char size_text[24];
            char modified_text[24];
            format_size_text(selected->size, selected->folder, size_text, sizeof(size_text));
            format_modified_text(selected->modified_tick, modified_text, sizeof(modified_text));
            gfx_draw_text(px + 16, layout.body_y + 116, "Size", RGB(100, 112, 124), 1);
            gfx_draw_text(px + 74, layout.body_y + 116, size_text, theme->text, 1);
            gfx_draw_text(px + 16, layout.body_y + 140, "Modified", RGB(100, 112, 124), 1);
            gfx_draw_text(px + 88, layout.body_y + 140, modified_text, theme->text, 1);
            gfx_draw_text(px + 16, layout.body_y + 166, "Path", RGB(100, 112, 124), 1);
            draw_text_trimmed(px + 16, layout.body_y + 184, selected->path, 22, theme->text);
            if (!selected->folder) {
                const FsFile *file = fs_find(selected->path);
                gfx_draw_text(px + 16, layout.body_y + 214, "Preview", RGB(100, 112, 124), 1);
                draw_text_trimmed(px + 16, layout.body_y + 232, file && file->contents[0] ? file->contents : "(empty file)", 22, theme->text);
            }
        } else {
            gfx_draw_text(px + 16, layout.body_y + 50, "Select a file or folder", RGB(92, 104, 116), 1);
            gfx_draw_text(px + 16, layout.body_y + 70, "Details appear here.", RGB(112, 124, 136), 1);
        }
        if (files_clipboard_mode != FILE_CLIPBOARD_EMPTY) {
            gfx_draw_text(px + 16, y + height - 34, files_clipboard_mode == FILE_CLIPBOARD_COPY ? "Clipboard: copy" : "Clipboard: cut",
                          RGB(88, 98, 108), 1);
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

static void draw_settings_nav_glyph(i32 kind, i32 x, i32 y, Color color) {
    if (kind == 0) {
        gfx_draw_rect(x + 4, y + 7, 16, 13, color);
        gfx_draw_line(x + 3, y + 8, x + 12, y + 2, color);
        gfx_draw_line(x + 12, y + 2, x + 21, y + 8, color);
    } else if (kind == 1) {
        gfx_draw_line(x + 12, y + 3, x + 12, y + 21, color);
        gfx_draw_line(x + 12, y + 3, x + 18, y + 8, color);
        gfx_draw_line(x + 18, y + 8, x + 12, y + 13, color);
        gfx_draw_line(x + 12, y + 13, x + 18, y + 18, color);
        gfx_draw_line(x + 18, y + 18, x + 12, y + 21, color);
    } else if (kind == 2) {
        gfx_draw_line(x + 5, y + 13, x + 12, y + 7, color);
        gfx_draw_line(x + 12, y + 7, x + 19, y + 13, color);
        gfx_draw_line(x + 2, y + 9, x + 12, y + 2, color);
        gfx_draw_line(x + 12, y + 2, x + 22, y + 9, color);
        gfx_draw_line(x + 8, y + 17, x + 12, y + 13, color);
        gfx_draw_line(x + 12, y + 13, x + 16, y + 17, color);
    } else if (kind == 3) {
        gfx_draw_line(x + 5, y + 18, x + 18, y + 5, color);
        gfx_draw_line(x + 8, y + 20, x + 20, y + 8, color);
        gfx_fill_circle_alpha(x + 18, y + 5, 3, color, 190);
    } else if (kind == 4) {
        gfx_draw_round_rect_alpha(x + 4, y + 4, 7, 7, 2, color, 190);
        gfx_draw_round_rect_alpha(x + 14, y + 4, 7, 7, 2, color, 190);
        gfx_draw_round_rect_alpha(x + 4, y + 14, 7, 7, 2, color, 190);
        gfx_draw_round_rect_alpha(x + 14, y + 14, 7, 7, 2, color, 190);
    } else if (kind == 5) {
        gfx_fill_circle_alpha(x + 12, y + 7, 5, color, 150);
        gfx_draw_round_rect_alpha(x + 5, y + 15, 14, 7, 4, color, 180);
    } else if (kind == 6) {
        gfx_draw_round_rect_alpha(x + 5, y + 5, 14, 14, 7, color, 180);
        gfx_draw_line(x + 12, y + 12, x + 12, y + 6, color);
        gfx_draw_line(x + 12, y + 12, x + 17, y + 12, color);
    } else if (kind == 7) {
        gfx_draw_line(x + 4, y + 7, x + 20, y + 7, color);
        gfx_draw_line(x + 12, y + 7, x + 12, y + 21, color);
        gfx_draw_line(x + 6, y + 13, x + 18, y + 13, color);
        gfx_draw_line(x + 7, y + 21, x + 12, y + 13, color);
        gfx_draw_line(x + 17, y + 21, x + 12, y + 13, color);
    } else if (kind == 8) {
        gfx_draw_line(x + 4, y + 9, x + 8, y + 5, color);
        gfx_draw_line(x + 4, y + 9, x + 8, y + 13, color);
        gfx_draw_line(x + 4, y + 9, x + 19, y + 9, color);
        gfx_draw_line(x + 20, y + 15, x + 16, y + 11, color);
        gfx_draw_line(x + 20, y + 15, x + 16, y + 19, color);
        gfx_draw_line(x + 5, y + 15, x + 20, y + 15, color);
    } else {
        gfx_draw_round_rect_alpha(x + 5, y + 5, 14, 14, 7, color, 180);
        gfx_draw_text(x + 10, y + 8, "i", color, 1);
    }
}

static void draw_settings_nav_item(i32 x, i32 y, i32 width, const char *label, i32 glyph, bool selected) {
    Color icon = selected ? RGB(255, 255, 255) : RGB(221, 228, 255);
    if (selected) {
        gfx_blur_round_rect(x, y, width, 34, 12);
        gfx_refract_round_rect_edges(x, y, width, 34, 12, 1);
        gfx_fill_round_rect_plain_alpha(x, y, width, 34, 12, RGB(230, 235, 255), 70);
        gfx_draw_round_rect_alpha(x, y, width, 34, 12, RGB(247, 250, 255), 56);
    }
    gfx_fill_round_rect_alpha(x + 12, y + 8, 18, 18, 5, RGB(255, 255, 255), selected ? 70 : 34);
    draw_settings_nav_glyph(glyph, x + 9, y + 5, icon);
    gfx_draw_text(x + 46, y + 12, label, RGB(248, 250, 255), 1);
}

static void draw_settings_search(i32 x, i32 y, i32 width) {
    gfx_blur_round_rect(x, y, width, 40, 14);
    gfx_refract_round_rect_edges(x, y, width, 40, 14, 1);
    gfx_fill_round_rect_plain_alpha(x, y, width, 40, 14, RGB(250, 252, 255),
                                    settings_search_editing ? 50 : 34);
    gfx_draw_round_rect_alpha(x, y, width, 40, 14, RGB(250, 252, 255),
                              settings_search_editing ? 66 : 36);
    const char *label = settings_search[0] ? settings_search : "Search settings";
    Color text = settings_search[0] ? RGB(255, 255, 255) : RGB(232, 238, 255);
    gfx_draw_text(x + 18, y + 15, label, text, 1);
    gfx_draw_round_rect_alpha(x + width - 30, y + 12, 12, 12, 6, RGB(239, 244, 255), 150);
    gfx_draw_line(x + width - 20, y + 23, x + width - 14, y + 29, RGB(232, 238, 255));
}

static void draw_settings_device_preview(i32 x, i32 y, i32 width, i32 height) {
    gfx_fill_round_rect_plain_alpha(x, y, width, height, 12, RGB(191, 196, 245), 245);
    gfx_fill_round_rect_alpha(x, y, width, height / 2, 12, RGB(255, 226, 220), 98);
    gfx_fill_round_rect_alpha(x, y + height / 2, width, height / 2, 12, RGB(75, 98, 170), 120);
    gfx_draw_line(x + 6, y + height - 28, x + width / 3, y + height / 2, RGB(65, 78, 132));
    gfx_draw_line(x + width / 3, y + height / 2, x + width / 2, y + height - 25, RGB(65, 78, 132));
    gfx_draw_line(x + width / 2, y + height - 26, x + width - 8, y + height / 2 + 8, RGB(55, 70, 124));
    gfx_fill_rect(x + 8, y + height - 19, width - 16, 2, RGB(236, 214, 236));
    gfx_draw_round_rect_alpha(x, y, width, height, 12, RGB(255, 255, 255), 70);
}

static void draw_settings_row_icon(i32 kind, i32 x, i32 y) {
    gfx_blur_round_rect(x, y, 42, 42, 12);
    gfx_refract_round_rect_edges(x, y, 42, 42, 12, 1);
    gfx_fill_round_rect_plain_alpha(x, y, 42, 42, 12, RGB(237, 242, 255), 54);
    gfx_draw_round_rect_alpha(x, y, 42, 42, 12, RGB(250, 252, 255), 38);
    Color c = RGB(255, 255, 255);
    if (kind == 0) {
        gfx_fill_circle_alpha(x + 21, y + 21, 5, c, 220);
        gfx_draw_line(x + 21, y + 8, x + 21, y + 13, c);
        gfx_draw_line(x + 21, y + 29, x + 21, y + 34, c);
        gfx_draw_line(x + 8, y + 21, x + 13, y + 21, c);
        gfx_draw_line(x + 29, y + 21, x + 34, y + 21, c);
    } else if (kind == 1) {
        gfx_draw_line(x + 13, y + 18, x + 20, y + 18, c);
        gfx_draw_line(x + 20, y + 18, x + 28, y + 11, c);
        gfx_draw_line(x + 20, y + 18, x + 28, y + 25, c);
        gfx_draw_line(x + 13, y + 25, x + 20, y + 25, c);
        gfx_draw_line(x + 31, y + 15, x + 31, y + 29, c);
    } else if (kind == 2) {
        gfx_draw_round_rect_alpha(x + 15, y + 11, 12, 18, 6, c, 200);
        gfx_draw_line(x + 12, y + 30, x + 30, y + 30, c);
    } else if (kind == 3) {
        gfx_fill_circle_alpha(x + 24, y + 21, 10, c, 210);
        gfx_fill_circle_alpha(x + 28, y + 16, 9, RGB(120, 140, 210), 245);
    } else {
        gfx_draw_round_rect_alpha(x + 14, y + 12, 14, 18, 7, c, 190);
        gfx_draw_line(x + 21, y + 9, x + 21, y + 21, c);
    }
}

static void draw_settings_system_row(i32 x, i32 y, i32 width, i32 kind, const char *title, const char *subtitle) {
    draw_settings_row_icon(kind, x + 16, y + 8);
    gfx_draw_text(x + 72, y + 16, title, RGB(250, 252, 255), 1);
    gfx_draw_text(x + 72, y + 36, subtitle, RGB(226, 232, 255), 1);
    gfx_draw_text(x + width - 28, y + 24, ">", RGB(250, 252, 255), 2);
}

static void draw_settings_window(i32 x, i32 y, i32 width, i32 height) {
    i32 sidebar_w = width < 760 ? 218 : 258;
    i32 main_x = x + sidebar_w + 36;
    i32 main_w = width - sidebar_w - 70;
    i32 nav_w = sidebar_w - 36;
    const DesktopTheme *theme = &themes[current_theme];

    gfx_blur_round_rect(x, y, width, height, 20);
    gfx_refract_round_rect_edges(x, y, width, height, 20, 1);
    gfx_fill_round_rect_plain_alpha(x, y, width, height, 20, RGB(248, 250, 255), 30);
    gfx_fill_round_rect_alpha(x, y, width, height, 20, RGB(255, 255, 255), 10);
    gfx_draw_round_rect_alpha(x, y, width, height, 20, RGB(246, 250, 255), 60);

    gfx_fill_round_rect_plain_alpha(x, y, sidebar_w, height, 20, RGB(255, 255, 255), 18);
    gfx_fill_round_rect_plain_alpha(x + sidebar_w - 1, y + 10, 1, height - 20, 0, RGB(255, 255, 255), 54);

    gfx_fill_round_rect_alpha(x + 24, y + 22, 26, 26, 7, RGB(242, 246, 255), 94);
    gfx_fill_round_rect_alpha(x + 32, y + 29, 8, 16, 4, RGB(118, 134, 224), 230);
    gfx_fill_round_rect_alpha(x + 40, y + 30, 8, 8, 4, RGB(255, 255, 255), 180);
    gfx_draw_text(x + 64, y + 30, "Settings", RGB(250, 252, 255), 1);
    draw_settings_search(x + 22, y + 70, nav_w);

    i32 nav_y = y + 122;
    draw_settings_nav_item(x + 18, nav_y + 0 * 42, nav_w, "System", 0, settings_page == 0);
    draw_settings_nav_item(x + 18, nav_y + 1 * 42, nav_w, "Bluetooth & devices", 1, settings_page == 1);
    draw_settings_nav_item(x + 18, nav_y + 2 * 42, nav_w, "Network & internet", 2, settings_page == 2);
    draw_settings_nav_item(x + 18, nav_y + 3 * 42, nav_w, "Personalization", 3, settings_page == 3);
    draw_settings_nav_item(x + 18, nav_y + 4 * 42, nav_w, "Apps", 4, settings_page == 4);
    draw_settings_nav_item(x + 18, nav_y + 5 * 42, nav_w, "Accounts", 5, settings_page == 5);
    draw_settings_nav_item(x + 18, nav_y + 6 * 42, nav_w, "Time & language", 6, settings_page == 6);
    draw_settings_nav_item(x + 18, nav_y + 7 * 42, nav_w, "Accessibility", 7, settings_page == 7);
    draw_settings_nav_item(x + 18, nav_y + 8 * 42, nav_w, "Updates", 8, settings_page == 8);
    draw_settings_nav_item(x + 18, nav_y + 9 * 42, nav_w, "About", 9, settings_page == 9);

    gfx_draw_text(main_x, y + 58, settings_page_label(settings_page), RGB(250, 252, 255), 2);
    gfx_draw_text(main_x, y + 92, settings_page_subtitle(settings_page), RGB(230, 236, 255), 1);

    i32 device_y = y + 132;
    gfx_blur_round_rect(main_x, device_y, main_w, 100, 18);
    gfx_refract_round_rect_edges(main_x, device_y, main_w, 100, 18, 1);
    gfx_fill_round_rect_plain_alpha(main_x, device_y, main_w, 100, 18, RGB(255, 255, 255), 36);
    gfx_fill_round_rect_plain_alpha(main_x + 2, device_y + 2, main_w - 4, 34, 16, theme->accent, 10);
    gfx_draw_round_rect_alpha(main_x, device_y, main_w, 100, 18, RGB(250, 252, 255), 44);
    if (settings_page == 0 || settings_page == 9) {
        draw_settings_device_preview(main_x + 18, device_y + 16, 150, 68);
        gfx_draw_text(main_x + 188, device_y + 26, settings_page == 0 ? "Aurora" : "LiquidOS", RGB(250, 252, 255), 2);
        gfx_draw_text(main_x + 188, device_y + 58, "Developer Preview", RGB(226, 232, 255), 1);
        gfx_draw_text(main_x + 188, device_y + 78, fs_persistence_available() ? "Storage: disk-backed" : "Storage: RAM only", RGB(205, 214, 250), 1);
    } else {
        draw_settings_row_icon(settings_page % 5, main_x + 28, device_y + 28);
        gfx_draw_text(main_x + 92, device_y + 26, settings_page_label(settings_page), RGB(250, 252, 255), 2);
        gfx_draw_text(main_x + 92, device_y + 58, settings_page_subtitle(settings_page), RGB(226, 232, 255), 1);
        gfx_draw_text(main_x + 92, device_y + 78, "Choose a row below to apply or open it.", RGB(205, 214, 250), 1);
    }

    i32 list_y = y + 244;
    i32 row_h = 62;
    i32 step = 68;
    gfx_blur_round_rect(main_x, list_y, main_w, step * 5 - 6, 18);
    gfx_refract_round_rect_edges(main_x, list_y, main_w, step * 5 - 6, 18, 1);
    gfx_fill_round_rect_plain_alpha(main_x, list_y, main_w, step * 5 - 6, 18, RGB(255, 255, 255), 30);
    gfx_draw_round_rect_alpha(main_x, list_y, main_w, step * 5 - 6, 18, RGB(250, 252, 255), 38);

    for (i32 i = 0; i < 5; i++) {
        draw_settings_system_row(main_x, list_y + i * step, main_w, i,
                                 settings_row_title(settings_page, i),
                                 settings_row_subtitle(settings_page, i));
        if (i < 4) {
            gfx_fill_round_rect_plain_alpha(main_x + 16, list_y + i * step + row_h,
                                            main_w - 32, 1, 0, RGB(255, 255, 255), 42);
        }
    }
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

static void window_content_rect(const Window *window, i32 *x, i32 *y, i32 *width, i32 *height) {
    if (window->expanded) {
        *x = window->x;
        *y = window->y;
        *width = window->width;
        *height = window->height;
        return;
    }
    *x = window->x + 6;
    *y = window->y + 30;
    *width = window->width - 12;
    *height = window->height - 36;
}

static bool has_expanded_window(void) {
    for (i32 i = 0; i < WINDOW_COUNT; i++) {
        Window *window = &windows[i];
        if (window->open && !window->minimized && window->expanded) {
            return true;
        }
    }
    return false;
}

static Window *expanded_chrome_window(void) {
    if (windows[focused_window].open && !windows[focused_window].minimized && windows[focused_window].expanded) {
        return &windows[focused_window];
    }
    for (int z = WINDOW_COUNT - 1; z >= 0; z--) {
        Window *window = &windows[z_order[z]];
        if (window->open && !window->minimized && window->expanded) {
            return window;
        }
    }
    return NULL;
}

static void draw_window(WindowKind kind) {
    Window *window = &windows[kind];
    if (!window->open || window->minimized) {
        return;
    }

    bool focused = focused_window == kind;
    if (!window->expanded) {
        draw_window_frame(window, focused);
    }

    i32 content_x;
    i32 content_y;
    i32 content_w;
    i32 content_h;
    window_content_rect(window, &content_x, &content_y, &content_w, &content_h);

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
    i32 radius = dock_reference_radius();
    i32 tile_size = dock_tile_size();
    i32 child_radius = dock_child_radius(dock_scale_value(compact ? 48 : 54), bar.h, radius);
    i32 icon_size = tile_size - (compact ? 9 : 10);

    draw_dock_glass_capsule(bar.x, bar.y, bar.w, bar.h, radius);
    draw_dock_clock(&bar);

    for (i32 i = 0; i < bar.slots_available; i++) {
        const DockApp *app = &dock_apps[i];
        draw_dock_icon_asset(bar.slot_x + i * bar.slot_step, bar.slot_y,
                             tile_size, child_radius, icon_size,
                             app->pixels, app->icon_width, app->icon_height,
                             app->tile_top, app->tile_bottom, app->small_artwork);
        if (app->window == WINDOW_SETTINGS) {
            draw_settings_dock_glyph(bar.slot_x + i * bar.slot_step, bar.slot_y, tile_size);
        }
    }
    if (dock_grip_visible || dock_resizing || hover_zone == 0 || hover_zone == 2) {
        draw_dock_resize_grip(&bar);
    }
}

static void draw_fullscreen_reveal(void) {
    if (!has_expanded_window() || !fullscreen_chrome_visible) {
        return;
    }
    i32 h = taskbar_y() + taskbar_h() + 18;
    draw_liquid_reference_glass(0, -18, (i32)gfx_width(), h + 18, 0, false);
    gfx_fill_round_rect_plain_alpha(0, h - 1, (i32)gfx_width(), 1, 0, RGB(94, 94, 94), 180);
    draw_taskbar();
    Window *window = expanded_chrome_window();
    if (window) {
        draw_window_chrome_group(window, true);
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
    windows[WINDOW_FILES] = make_window(240, 185, 820, 520, "Files");
    windows[WINDOW_STORE] = make_window(240, 170, 720, 430, "Store");
    windows[WINDOW_SETTINGS] = make_window(160, 160, 960, 620, "Settings");
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
        } else if (focused_window == WINDOW_FILES && windows[WINDOW_FILES].open) {
            files_on_char(event->ch);
            mark_dirty_window(WINDOW_FILES);
        } else if (focused_window == WINDOW_SETTINGS && windows[WINDOW_SETTINGS].open) {
            settings_on_char(event->ch);
            mark_dirty_window(WINDOW_SETTINGS);
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

    bool expanded_window_open = has_expanded_window();
    bool next_fullscreen_chrome = expanded_window_open &&
                                  (mouse_y <= 3 ||
                                   (fullscreen_chrome_visible && mouse_y <= taskbar_y() + taskbar_h() + 42));
    if (next_fullscreen_chrome != fullscreen_chrome_visible) {
        fullscreen_chrome_visible = next_fullscreen_chrome;
        mark_dirty_full();
    }

    i32 new_hover_zone = (!expanded_window_open || fullscreen_chrome_visible) ? taskbar_hover_zone_at(mouse_x, mouse_y) : -1;
    if (new_hover_zone != hover_zone) {
        hover_zone = new_hover_zone;
    }

    bool left_now = event->left_down;
    bool right_now = event->right_down;
    bool taskbar_available = !expanded_window_open || fullscreen_chrome_visible;

    if (right_now && !previous_right &&
        (!taskbar_available || !point_in_rect(mouse_x, mouse_y, taskbar_x(), taskbar_y(), taskbar_w(), taskbar_h())) &&
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
            mark_dirty_taskbar();
            dock_scale_percent = next_scale;
            mark_dirty_taskbar();
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
        if (taskbar_available && point_in_rect(mouse_x, mouse_y, taskbar_x(), taskbar_y(), taskbar_w(), taskbar_h())) {
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
    ui_stats.render_calls++;
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
        if (has_expanded_window()) {
            draw_fullscreen_reveal();
        } else {
            draw_taskbar();
        }
        draw_notifications();
        gfx_clear_clip();
        gfx_present_rect(present_x, present_y, present_w, present_h);
        gfx_present_cursor(mouse_x, mouse_y, mouse_x, mouse_y);
        ui_stats.presented_frames++;
        ui_stats.full_redraws++;
        ui_stats.dirty_pixels += (u64)(present_w > 0 ? present_w : 0) * (u64)(present_h > 0 ? present_h : 0);
        previous_mouse_x = mouse_x;
        previous_mouse_y = mouse_y;
        full_redraw_needed = false;
        dirty_region_valid = false;
        cursor_redraw_needed = false;
        return;
    }

    if (cursor_redraw_needed) {
        gfx_present_cursor(previous_mouse_x, previous_mouse_y, mouse_x, mouse_y);
        ui_stats.cursor_presents++;
        previous_mouse_x = mouse_x;
        previous_mouse_y = mouse_y;
        cursor_redraw_needed = false;
    }
}

UiPerformanceStats ui_performance_stats(void) {
    return ui_stats;
}
