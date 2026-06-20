#include <liquidos/gfx.h>
#include <liquidos/lib.h>
#include <liquidos/liqueia.h>
#include <liquidos/network.h>

#define LIQUEIA_MAX_TABS 3
#define LIQUEIA_MAX_HISTORY 6
#define LIQUEIA_URL_LENGTH 96

typedef enum LiqueiaPage {
    LIQUEIA_NEW_TAB,
    LIQUEIA_BOOKMARKS,
    LIQUEIA_HISTORY,
    LIQUEIA_SETTINGS,
    LIQUEIA_WEB,
    LIQUEIA_OFFLINE
} LiqueiaPage;

typedef struct LiqueiaTab {
    char address[LIQUEIA_URL_LENGTH];
    char previous[LIQUEIA_URL_LENGTH];
    char forward[LIQUEIA_URL_LENGTH];
    LiqueiaPage page;
    LiqueiaPage previous_page;
    LiqueiaPage forward_page;
    bool bookmarked;
} LiqueiaTab;

static LiqueiaTab tabs[LIQUEIA_MAX_TABS];
static char history[LIQUEIA_MAX_HISTORY][LIQUEIA_URL_LENGTH];
static i32 tab_count;
static i32 active_tab;
static i32 history_count;
static bool editing_address;
static char address_before_edit[LIQUEIA_URL_LENGTH];
static LiqueiaPage page_before_edit;

static bool point_in_rect(i32 px, i32 py, i32 x, i32 y, i32 width, i32 height) {
    return px >= x && py >= y && px < x + width && py < y + height;
}

static bool starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void set_address(LiqueiaTab *tab, const char *address) {
    strncpy(tab->address, address, LIQUEIA_URL_LENGTH - 1);
    tab->address[LIQUEIA_URL_LENGTH - 1] = 0;
}

static LiqueiaPage page_for_address(const char *address) {
    if (!address[0] || strcmp(address, "liqueia://newtab") == 0) {
        return LIQUEIA_NEW_TAB;
    }
    if (strcmp(address, "liqueia://bookmarks") == 0) {
        return LIQUEIA_BOOKMARKS;
    }
    if (strcmp(address, "liqueia://history") == 0) {
        return LIQUEIA_HISTORY;
    }
    if (strcmp(address, "liqueia://settings") == 0) {
        return LIQUEIA_SETTINGS;
    }
    if (network_fetch(address).ok) {
        return LIQUEIA_WEB;
    }
    return LIQUEIA_OFFLINE;
}

static void add_history(const char *address) {
    if (!address[0] || starts_with(address, "liqueia://")) {
        return;
    }
    for (i32 i = LIQUEIA_MAX_HISTORY - 1; i > 0; i--) {
        strcpy(history[i], history[i - 1]);
    }
    strncpy(history[0], address, LIQUEIA_URL_LENGTH - 1);
    history[0][LIQUEIA_URL_LENGTH - 1] = 0;
    if (history_count < LIQUEIA_MAX_HISTORY) {
        history_count++;
    }
}

static void navigate_to(const char *address, bool record_navigation) {
    LiqueiaTab *tab = &tabs[active_tab];
    if (record_navigation) {
        strcpy(tab->previous, editing_address ? address_before_edit : tab->address);
        tab->previous_page = editing_address ? page_before_edit : tab->page;
        tab->forward[0] = 0;
    }
    set_address(tab, address[0] ? address : "liqueia://newtab");
    tab->page = page_for_address(tab->address);
    tab->bookmarked = false;
    add_history(tab->address);
    editing_address = false;
}

static void navigate_back(void) {
    LiqueiaTab *tab = &tabs[active_tab];
    if (!tab->previous[0]) {
        return;
    }
    strcpy(tab->forward, tab->address);
    tab->forward_page = tab->page;
    strcpy(tab->address, tab->previous);
    tab->page = tab->previous_page;
    tab->previous[0] = 0;
    editing_address = false;
}

static void navigate_forward(void) {
    LiqueiaTab *tab = &tabs[active_tab];
    if (!tab->forward[0]) {
        return;
    }
    strcpy(tab->previous, tab->address);
    tab->previous_page = tab->page;
    strcpy(tab->address, tab->forward);
    tab->page = tab->forward_page;
    tab->forward[0] = 0;
    editing_address = false;
}

static void new_tab(void) {
    if (tab_count < LIQUEIA_MAX_TABS) {
        active_tab = tab_count++;
    } else {
        active_tab = (active_tab + 1) % tab_count;
    }
    memset(&tabs[active_tab], 0, sizeof(tabs[active_tab]));
    set_address(&tabs[active_tab], "liqueia://newtab");
    tabs[active_tab].page = LIQUEIA_NEW_TAB;
    editing_address = false;
}

static void begin_address_edit(void) {
    LiqueiaTab *tab = &tabs[active_tab];
    strcpy(address_before_edit, tab->address);
    page_before_edit = tab->page;
    tab->address[0] = 0;
    editing_address = true;
}

void liqueia_init(void) {
    memset(tabs, 0, sizeof(tabs));
    memset(history, 0, sizeof(history));
    tab_count = 0;
    active_tab = 0;
    history_count = 0;
    editing_address = false;
    address_before_edit[0] = 0;
    new_tab();
}

void liqueia_on_char(char ch) {
    if (!editing_address) {
        return;
    }

    LiqueiaTab *tab = &tabs[active_tab];
    size_t length = strlen(tab->address);
    if (ch == '\n') {
        navigate_to(tab->address, true);
    } else if (ch == '\b') {
        if (length > 0) {
            tab->address[length - 1] = 0;
        }
    } else if (ch >= 32 && ch <= 126 && length + 1 < LIQUEIA_URL_LENGTH) {
        tab->address[length] = ch;
        tab->address[length + 1] = 0;
    }
}

void liqueia_handle_click(i32 x, i32 y, i32 width, i32 height, i32 mouse_x, i32 mouse_y) {
    (void)height;
    LiqueiaTab *tab = &tabs[active_tab];
    i32 tab_width = width > 620 ? 154 : 112;

    for (i32 i = 0; i < tab_count; i++) {
        if (point_in_rect(mouse_x, mouse_y, x + 48 + i * (tab_width + 6), y + 8, tab_width, 32)) {
            active_tab = i;
            editing_address = false;
            return;
        }
    }
    if (point_in_rect(mouse_x, mouse_y, x + 54 + tab_count * (tab_width + 6), y + 12, 24, 24)) {
        new_tab();
        return;
    }
    if (point_in_rect(mouse_x, mouse_y, x + 18, y + 55, 30, 30)) {
        navigate_back();
    } else if (point_in_rect(mouse_x, mouse_y, x + 52, y + 55, 30, 30)) {
        navigate_forward();
    } else if (point_in_rect(mouse_x, mouse_y, x + 86, y + 55, 30, 30)) {
        navigate_to(tab->address, false);
    } else if (point_in_rect(mouse_x, mouse_y, x + 126, y + 51, width - 244, 38)) {
        begin_address_edit();
    } else if (point_in_rect(mouse_x, mouse_y, x + width - 104, y + 56, 32, 28)) {
        tab->bookmarked = !tab->bookmarked;
    } else if (point_in_rect(mouse_x, mouse_y, x + 22, y + 108, 104, 30)) {
        navigate_to("liqueia://newtab", true);
    } else if (point_in_rect(mouse_x, mouse_y, x + 136, y + 108, 104, 30)) {
        navigate_to("liqueia://bookmarks", true);
    } else if (point_in_rect(mouse_x, mouse_y, x + 250, y + 108, 92, 30)) {
        navigate_to("liqueia://history", true);
    } else if (point_in_rect(mouse_x, mouse_y, x + 352, y + 108, 92, 30)) {
        navigate_to("liqueia://settings", true);
    } else if (tab->page == LIQUEIA_NEW_TAB &&
               point_in_rect(mouse_x, mouse_y, x + 56, y + 230, width - 112, 54)) {
        begin_address_edit();
    }
}

static void draw_logo(i32 x, i32 y, i32 size) {
    gfx_fill_round_rect_plain_alpha(x, y, size, size, size / 3, RGB(13, 14, 19), 255);
    gfx_draw_round_rect(x + 2, y + 2, size - 4, size - 4, size / 3, RGB(94, 80, 54));
    gfx_fill_rect(x + size / 3, y + size / 5, size / 5, size * 3 / 5, RGB(216, 170, 88));
    gfx_fill_round_rect(x + size / 3, y + size * 3 / 5, size / 2, size / 5, size / 10, RGB(216, 170, 88));
}

static void draw_nav_button(i32 x, i32 y, const char *label, bool enabled) {
    gfx_fill_round_rect_plain_alpha(x, y, 30, 30, 12, RGB(31, 32, 39), 255);
    gfx_draw_text(x + 10, y + 7, label, enabled ? RGB(238, 221, 186) : RGB(103, 103, 110), 1);
}

static void draw_chip(i32 x, i32 y, i32 width, const char *label, bool active) {
    gfx_fill_round_rect_plain_alpha(x, y, width, 30, 12, active ? RGB(216, 170, 88) : RGB(35, 36, 43), 255);
    gfx_draw_text(x + 13, y + 7, label, active ? RGB(28, 23, 15) : RGB(207, 207, 214), 1);
}

static void draw_text_trimmed(i32 x, i32 y, const char *text, size_t max_chars, Color color) {
    char line[72];
    size_t count = strlen(text);
    if (count > max_chars) {
        count = max_chars;
    }
    if (count >= sizeof(line)) {
        count = sizeof(line) - 1;
    }
    memcpy(line, text, count);
    line[count] = 0;
    gfx_draw_text(x, y, line, color, 1);
}

static void draw_new_tab(i32 x, i32 y, i32 width, i32 height) {
    gfx_fill_round_rect_plain_alpha(x + 24, y + 152, width - 48, height - 172, 24, RGB(19, 20, 27), 255);
    gfx_fill_circle_alpha(x + width - 110, y + 205, 72, RGB(216, 170, 88), 28);
    draw_logo(x + 54, y + 176, 50);
    gfx_draw_text(x + 124, y + 180, "A calmer way to explore.", RGB(246, 238, 222), 2);
    gfx_draw_text(x + 124, y + 209, "Liqueia for LiquidOS", RGB(216, 170, 88), 1);
    gfx_fill_round_rect_plain_alpha(x + 56, y + 230, width - 112, 54, 20, RGB(37, 38, 46), 255);
    gfx_draw_text(x + 76, y + 248, "Search or enter an address", RGB(166, 166, 174), 1);
    gfx_draw_text(x + 56, y + 305, "NATIVE APP", RGB(216, 170, 88), 1);
    gfx_draw_text(x + 56, y + 326, "Tabs, local pages, history, bookmarks and keyboard input are ready.", RGB(205, 205, 212), 1);
    gfx_draw_text(x + 56, y + 347, "Try http://liquidos.local/ or http://liquidos.local/store", RGB(136, 137, 146), 1);
}

static void draw_list_page(i32 x, i32 y, i32 width, i32 height, LiqueiaPage page) {
    gfx_fill_round_rect_plain_alpha(x + 24, y + 152, width - 48, height - 172, 24, RGB(24, 25, 32), 255);
    const char *title = page == LIQUEIA_BOOKMARKS ? "Saved places" :
                        page == LIQUEIA_HISTORY ? "Recent journeys" : "Liqueia settings";
    gfx_draw_text(x + 50, y + 178, title, RGB(246, 238, 222), 2);

    if (page == LIQUEIA_SETTINGS) {
        gfx_draw_text(x + 50, y + 224, "Search engine", RGB(153, 153, 162), 1);
        gfx_draw_text(x + width - 180, y + 224, "Google", RGB(216, 170, 88), 1);
        gfx_draw_text(x + 50, y + 252, "Do Not Track", RGB(153, 153, 162), 1);
        gfx_draw_text(x + width - 180, y + 252, "Enabled", RGB(216, 170, 88), 1);
        gfx_draw_text(x + 50, y + 280, "Network", RGB(153, 153, 162), 1);
        gfx_draw_text(x + width - 180, y + 280, network_info()->driver, RGB(216, 170, 88), 1);
        return;
    }

    if (page == LIQUEIA_BOOKMARKS) {
        bool found = false;
        i32 row = 0;
        for (i32 i = 0; i < tab_count; i++) {
            if (!tabs[i].bookmarked) {
                continue;
            }
            draw_text_trimmed(x + 52, y + 224 + row * 27, tabs[i].address, 54, RGB(205, 205, 212));
            found = true;
            row++;
        }
        if (!found) {
            gfx_draw_text(x + 50, y + 224, "No saved places yet. Use the star in the address bar.", RGB(153, 153, 162), 1);
        }
        return;
    }

    if (history_count == 0) {
        gfx_draw_text(x + 50, y + 224, "No external journeys yet.", RGB(153, 153, 162), 1);
    }
    for (i32 i = 0; i < history_count; i++) {
        draw_text_trimmed(x + 52, y + 224 + i * 27, history[i], 54, RGB(205, 205, 212));
    }
}

static void draw_offline(i32 x, i32 y, i32 width, i32 height) {
    LiqueiaTab *tab = &tabs[active_tab];
    NetResponse fetched = network_fetch(tab->address);
    gfx_fill_round_rect_plain_alpha(x + 24, y + 152, width - 48, height - 172, 24, RGB(24, 25, 32), 255);
    gfx_fill_circle_alpha(x + width - 112, y + 210, 66, RGB(216, 170, 88), 20);
    gfx_draw_text(x + 52, y + 184, "This journey needs a network.", RGB(246, 238, 222), 2);
    gfx_draw_text(x + 52, y + 224, "Liqueia accepted the address:", RGB(153, 153, 162), 1);
    draw_text_trimmed(x + 52, y + 248, tab->address, 58, RGB(216, 170, 88));
    gfx_draw_text(x + 52, y + 286, fetched.message, RGB(205, 205, 212), 1);
    gfx_draw_text(x + 52, y + 307, "Use liquidos.local routes now; real NIC/TCP/TLS drivers come next.", RGB(136, 137, 146), 1);
}

static void draw_web_page(i32 x, i32 y, i32 width, i32 height) {
    LiqueiaTab *tab = &tabs[active_tab];
    NetResponse fetched = network_fetch(tab->address);
    gfx_fill_round_rect_plain_alpha(x + 24, y + 152, width - 48, height - 172, 24, RGB(24, 25, 32), 255);
    gfx_fill_circle_alpha(x + width - 110, y + 205, 72, RGB(86, 155, 255), 20);
    gfx_draw_text(x + 52, y + 184, fetched.status == 200 ? "Page loaded" : "Request failed", RGB(246, 238, 222), 2);
    gfx_draw_text(x + 52, y + 216, fetched.mime, RGB(216, 170, 88), 1);
    draw_text_trimmed(x + 52, y + 242, fetched.body, 60, RGB(205, 205, 212));

    const char *line = fetched.body;
    i32 row = 0;
    while (*line && row < 7) {
        char text[72];
        size_t used = 0;
        while (line[used] && line[used] != '\n' && used + 1 < sizeof(text)) {
            text[used] = line[used];
            used++;
        }
        text[used] = 0;
        if (text[0]) {
            gfx_draw_text(x + 52, y + 280 + row * 22, text, RGB(184, 184, 192), 1);
            row++;
        }
        line += used;
        if (*line == '\n') {
            line++;
        }
    }
}

void liqueia_render(i32 x, i32 y, i32 width, i32 height) {
    LiqueiaTab *tab = &tabs[active_tab];
    i32 tab_width = width > 620 ? 154 : 112;

    gfx_fill_rect(x, y, width, height, RGB(13, 14, 19));
    gfx_fill_rect(x, y, width, 46, RGB(18, 19, 25));
    draw_logo(x + 12, y + 8, 32);

    for (i32 i = 0; i < tab_count; i++) {
        i32 tab_x = x + 48 + i * (tab_width + 6);
        bool active = i == active_tab;
        gfx_fill_round_rect_plain_alpha(tab_x, y + 8, tab_width, 32, 12,
                                        active ? RGB(42, 42, 50) : RGB(25, 26, 32), 255);
        gfx_draw_text(tab_x + 13, y + 17, tabs[i].page == LIQUEIA_NEW_TAB ? "New Tab" : "Liqueia", 
                      active ? RGB(245, 235, 216) : RGB(148, 148, 156), 1);
    }
    gfx_fill_round_rect_plain_alpha(x + 54 + tab_count * (tab_width + 6), y + 12, 24, 24, 10, RGB(35, 36, 43), 255);
    gfx_draw_text(x + 62 + tab_count * (tab_width + 6), y + 16, "+", RGB(216, 170, 88), 1);

    gfx_fill_rect(x, y + 46, width, 52, RGB(21, 22, 28));
    draw_nav_button(x + 18, y + 55, "<", tab->previous[0] != 0);
    draw_nav_button(x + 52, y + 55, ">", tab->forward[0] != 0);
    draw_nav_button(x + 86, y + 55, "R", true);

    gfx_fill_round_rect_plain_alpha(x + 126, y + 51, width - 244, 38, 15,
                                    editing_address ? RGB(53, 49, 42) : RGB(31, 32, 39), 255);
    draw_text_trimmed(x + 144, y + 62, tab->address[0] ? tab->address : "Type an address and press Enter",
                      width > 700 ? 48 : 30, editing_address ? RGB(246, 238, 222) : RGB(174, 174, 182));
    gfx_fill_round_rect_plain_alpha(x + width - 104, y + 56, 32, 28, 11,
                                    tab->bookmarked ? RGB(216, 170, 88) : RGB(35, 36, 43), 255);
    gfx_draw_text(x + width - 93, y + 62, "*", tab->bookmarked ? RGB(28, 23, 15) : RGB(216, 170, 88), 1);
    gfx_fill_round_rect_plain_alpha(x + width - 66, y + 56, 48, 28, 11, RGB(37, 58, 48), 255);
    gfx_draw_text(x + width - 58, y + 63, "NET", RGB(164, 232, 190), 1);

    draw_chip(x + 22, y + 108, 104, "New tab", tab->page == LIQUEIA_NEW_TAB);
    draw_chip(x + 136, y + 108, 104, "Bookmarks", tab->page == LIQUEIA_BOOKMARKS);
    draw_chip(x + 250, y + 108, 92, "History", tab->page == LIQUEIA_HISTORY);
    draw_chip(x + 352, y + 108, 92, "Settings", tab->page == LIQUEIA_SETTINGS);

    if (tab->page == LIQUEIA_NEW_TAB) {
        draw_new_tab(x, y, width, height);
    } else if (tab->page == LIQUEIA_WEB) {
        draw_web_page(x, y, width, height);
    } else if (tab->page == LIQUEIA_OFFLINE) {
        draw_offline(x, y, width, height);
    } else {
        draw_list_page(x, y, width, height, tab->page);
    }
}
