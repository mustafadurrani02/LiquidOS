#include <liquidos/gfx.h>
#include <liquidos/lib.h>
#include <liquidos/liqueia.h>
#include <liquidos/network.h>

#define LIQUEIA_MAX_TABS 3
#define LIQUEIA_MAX_HISTORY 6
#define LIQUEIA_URL_LENGTH 384
#define LIQUEIA_BODY_LENGTH 16384
#define LIQUEIA_TEXT_LENGTH 3072
#define LIQUEIA_MIME_LENGTH 48

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
    char body[LIQUEIA_BODY_LENGTH];
    char display_text[LIQUEIA_TEXT_LENGTH];
    char mime[LIQUEIA_MIME_LENGTH];
    NetResponse response;
    LiqueiaPage page;
    LiqueiaPage previous_page;
    LiqueiaPage forward_page;
    bool bookmarked;
    bool response_ready;
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

static char ascii_lower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
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

static bool contains_char(const char *text, char ch) {
    while (*text) {
        if (*text == ch) {
            return true;
        }
        text++;
    }
    return false;
}

static bool contains_text_ci(const char *text, const char *needle) {
    if (!text || !needle || !needle[0]) {
        return false;
    }
    while (*text) {
        if (starts_with_ci(text, needle)) {
            return true;
        }
        text++;
    }
    return false;
}

static void set_address(LiqueiaTab *tab, const char *address) {
    strncpy(tab->address, address, LIQUEIA_URL_LENGTH - 1);
    tab->address[LIQUEIA_URL_LENGTH - 1] = 0;
}

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
}

static void copy_trimmed(const char *input, char *out, size_t out_size) {
    size_t start = 0;
    size_t end = strlen(input);
    while (input[start] == ' ' || input[start] == '\t') {
        start++;
    }
    while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\t')) {
        end--;
    }

    size_t used = 0;
    while (start < end && used + 1 < out_size) {
        out[used++] = input[start++];
    }
    out[used] = 0;
}

static bool is_url_unreserved(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static char hex_digit(u8 value) {
    value &= 0x0F;
    return value < 10 ? (char)('0' + value) : (char)('A' + value - 10);
}

static void append_url_encoded(char *dest, size_t dest_size, const char *text) {
    size_t used = strlen(dest);
    while (*text && used + 1 < dest_size) {
        unsigned char ch = (unsigned char)*text++;
        if (ch == ' ') {
            dest[used++] = '+';
        } else if (is_url_unreserved((char)ch)) {
            dest[used++] = (char)ch;
        } else if (used + 3 < dest_size) {
            dest[used++] = '%';
            dest[used++] = hex_digit((u8)(ch >> 4));
            dest[used++] = hex_digit((u8)ch);
        }
    }
    dest[used] = 0;
}

static void build_google_search_url(const char *query, char *out, size_t out_size) {
    out[0] = 0;
    append_text(out, out_size, "https://www.google.com/search?q=");
    append_url_encoded(out, out_size, query);
}

static void normalize_address_input(const char *input, char *out, size_t out_size) {
    char trimmed[LIQUEIA_URL_LENGTH];
    copy_trimmed(input, trimmed, sizeof(trimmed));
    if (!trimmed[0]) {
        strncpy(out, "liqueia://newtab", out_size - 1);
        out[out_size - 1] = 0;
        return;
    }

    if (starts_with_ci(trimmed, "http://") ||
        starts_with_ci(trimmed, "https://") ||
        starts_with_ci(trimmed, "liqueia://")) {
        strncpy(out, trimmed, out_size - 1);
        out[out_size - 1] = 0;
        return;
    }

    bool has_space = contains_char(trimmed, ' ') || contains_char(trimmed, '\t');
    bool looks_like_host = contains_char(trimmed, '.') ||
                           starts_with_ci(trimmed, "liquidos.local") ||
                           starts_with_ci(trimmed, "store.liquidos.local");
    if (!has_space && looks_like_host) {
        out[0] = 0;
        if (starts_with_ci(trimmed, "liquidos.local") ||
            starts_with_ci(trimmed, "store.liquidos.local") ||
            starts_with_ci(trimmed, "10.0.2.")) {
            append_text(out, out_size, "http://");
        } else {
            append_text(out, out_size, "https://");
        }
        append_text(out, out_size, trimmed);
        return;
    }

    build_google_search_url(trimmed, out, out_size);
}

static void append_display_line(char *out, size_t out_size, const char *line) {
    char trimmed[160];
    copy_trimmed(line, trimmed, sizeof(trimmed));
    if (strlen(trimmed) < 3) {
        return;
    }
    append_text(out, out_size, trimmed);
    append_text(out, out_size, "\n");
}

static const char *find_after_ci(const char *text, const char *needle) {
    size_t needle_len = strlen(needle);
    while (*text) {
        bool match = true;
        for (size_t i = 0; i < needle_len; i++) {
            if (!text[i] || ascii_lower(text[i]) != ascii_lower(needle[i])) {
                match = false;
                break;
            }
        }
        if (match) {
            return text + needle_len;
        }
        text++;
    }
    return text;
}

static void append_entity_char(char *line, size_t line_size, size_t *line_used, const char **cursor) {
    const char *p = *cursor;
    char ch = 0;
    if (starts_with(p, "&amp;")) {
        ch = '&';
        p += 5;
    } else if (starts_with(p, "&lt;")) {
        ch = '<';
        p += 4;
    } else if (starts_with(p, "&gt;")) {
        ch = '>';
        p += 4;
    } else if (starts_with(p, "&quot;")) {
        ch = '"';
        p += 6;
    } else if (starts_with(p, "&#39;")) {
        ch = '\'';
        p += 5;
    } else if (starts_with(p, "&nbsp;")) {
        ch = ' ';
        p += 6;
    } else {
        ch = ' ';
        while (*p && *p != ';' && p - *cursor < 10) {
            p++;
        }
        if (*p == ';') {
            p++;
        }
    }

    if (*line_used + 1 < line_size) {
        line[(*line_used)++] = ch;
        line[*line_used] = 0;
    }
    *cursor = p;
}

static void format_html_as_text(const char *html, char *out, size_t out_size) {
    out[0] = 0;
    char line[160];
    size_t line_used = 0;
    bool last_space = false;
    line[0] = 0;

    const char *p = html;
    while (*p && strlen(out) + 1 < out_size) {
        if (starts_with_ci(p, "<script")) {
            p = find_after_ci(p, "</script>");
            continue;
        }
        if (starts_with_ci(p, "<style")) {
            p = find_after_ci(p, "</style>");
            continue;
        }
        if (*p == '<') {
            bool line_break = starts_with_ci(p, "<br") ||
                              starts_with_ci(p, "<li") ||
                              starts_with_ci(p, "</a") ||
                              starts_with_ci(p, "</p") ||
                              starts_with_ci(p, "</div") ||
                              starts_with_ci(p, "</h");
            while (*p && *p != '>') {
                p++;
            }
            if (*p == '>') {
                p++;
            }
            if (line_break && line_used > 0) {
                append_display_line(out, out_size, line);
                line_used = 0;
                line[0] = 0;
                last_space = false;
            }
            continue;
        }

        if (*p == '&') {
            append_entity_char(line, sizeof(line), &line_used, &p);
            last_space = false;
            continue;
        }

        char ch = *p++;
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
        if (ch == ' ') {
            if (last_space || line_used == 0) {
                continue;
            }
            last_space = true;
        } else {
            last_space = false;
        }
        if (line_used + 1 < sizeof(line)) {
            line[line_used++] = ch;
            line[line_used] = 0;
        } else {
            append_display_line(out, out_size, line);
            line_used = 0;
            line[0] = 0;
            last_space = false;
        }
    }

    if (line_used > 0) {
        append_display_line(out, out_size, line);
    }
}

static void format_response_text(LiqueiaTab *tab) {
    tab->display_text[0] = 0;
    if (starts_with(tab->mime, "text/html")) {
        format_html_as_text(tab->body, tab->display_text, sizeof(tab->display_text));
        if (!tab->display_text[0]) {
            strncpy(tab->display_text, "HTML page loaded, but no readable text was found.", sizeof(tab->display_text) - 1);
            tab->display_text[sizeof(tab->display_text) - 1] = 0;
        }
    } else {
        strncpy(tab->display_text, tab->body, sizeof(tab->display_text) - 1);
        tab->display_text[sizeof(tab->display_text) - 1] = 0;
    }
}

static void cache_response(LiqueiaTab *tab, NetResponse fetched) {
    strncpy(tab->body, fetched.body ? fetched.body : "", sizeof(tab->body) - 1);
    tab->body[sizeof(tab->body) - 1] = 0;
    strncpy(tab->mime, fetched.mime ? fetched.mime : "text/plain", sizeof(tab->mime) - 1);
    tab->mime[sizeof(tab->mime) - 1] = 0;
    tab->response = fetched;
    tab->response.body = tab->body;
    tab->response.mime = tab->mime;
    tab->response.size = strlen(tab->body);
    tab->response_ready = true;
    format_response_text(tab);
}

static void clear_response(LiqueiaTab *tab) {
    tab->body[0] = 0;
    tab->display_text[0] = 0;
    strcpy(tab->mime, "text/plain");
    tab->response = (NetResponse){ false, 0, tab->mime, tab->body, 0, "" };
    tab->response_ready = false;
}

static LiqueiaPage load_page_for_tab(LiqueiaTab *tab) {
    clear_response(tab);
    const char *address = tab->address;
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
    NetResponse fetched = network_fetch(address);
    cache_response(tab, fetched);
    if (fetched.ok) {
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
    char normalized[LIQUEIA_URL_LENGTH];
    normalize_address_input(address, normalized, sizeof(normalized));
    if (record_navigation) {
        strcpy(tab->previous, editing_address ? address_before_edit : tab->address);
        tab->previous_page = editing_address ? page_before_edit : tab->page;
        tab->forward[0] = 0;
    }
    set_address(tab, normalized);
    tab->page = load_page_for_tab(tab);
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
    tab->page = load_page_for_tab(tab);
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
    tab->page = load_page_for_tab(tab);
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
    gfx_draw_text(x + 76, y + 248, "Search Google or enter an address", RGB(166, 166, 174), 1);
    gfx_draw_text(x + 56, y + 305, "NATIVE APP", RGB(216, 170, 88), 1);
    gfx_draw_text(x + 56, y + 326, "Type a search and press Enter. Liqueia uses Google by default.", RGB(205, 205, 212), 1);
    gfx_draw_text(x + 56, y + 347, "Try liquidos, example.com, or http://liquidos.local/store", RGB(136, 137, 146), 1);
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
    NetResponse fetched = tab->response;
    gfx_fill_round_rect_plain_alpha(x + 24, y + 152, width - 48, height - 172, 24, RGB(24, 25, 32), 255);
    gfx_fill_circle_alpha(x + width - 112, y + 210, 66, RGB(216, 170, 88), 20);
    gfx_draw_text(x + 52, y + 184, "This journey needs a network.", RGB(246, 238, 222), 2);
    gfx_draw_text(x + 52, y + 224, "Liqueia accepted the address:", RGB(153, 153, 162), 1);
    draw_text_trimmed(x + 52, y + 248, tab->address, 58, RGB(216, 170, 88));
    gfx_draw_text(x + 52, y + 286, fetched.message, RGB(205, 205, 212), 1);
    gfx_draw_text(x + 52, y + 307, "Run through scripts/run-qemu.sh for RTL8139 DNS/TCP/HTTP networking.", RGB(136, 137, 146), 1);

    const char *line = tab->display_text;
    i32 row = 0;
    while (*line && row < 4) {
        char text[72];
        size_t used = 0;
        while (line[used] && line[used] != '\n' && used + 1 < sizeof(text)) {
            text[used] = line[used];
            used++;
        }
        text[used] = 0;
        if (text[0]) {
            gfx_draw_text(x + 52, y + 340 + row * 22, text, RGB(184, 184, 192), 1);
            row++;
        }
        line += used;
        if (*line == '\n') {
            line++;
        }
    }
}

typedef struct WebStyle {
    Color color;
    Color bg;
    bool has_bg;
    bool glass;
    i32 radius;
    i32 padding;
    i32 margin;
    i32 width;
    i32 height;
    u32 scale;
} WebStyle;

static bool css_name_match(const char *text, const char *name) {
    while (*name) {
        if (ascii_lower(*text) != ascii_lower(*name)) {
            return false;
        }
        text++;
        name++;
    }
    return true;
}

static i32 css_number(const char *text) {
    while (*text && (*text < '0' || *text > '9')) {
        text++;
    }
    i32 value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value;
}

static bool copy_css_value(const char *css, const char *property, char *out, size_t out_size) {
    size_t property_len = strlen(property);
    for (const char *p = css; *p; p++) {
        bool boundary = p == css || p[-1] == ';' || p[-1] == '{' || p[-1] == ' ' || p[-1] == '\t';
        if (!boundary || !css_name_match(p, property)) {
            continue;
        }
        const char *q = p + property_len;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        if (*q != ':') {
            continue;
        }
        q++;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        size_t used = 0;
        while (*q && *q != ';' && *q != '}' && used + 1 < out_size) {
            out[used++] = *q++;
        }
        out[used] = 0;
        return used > 0;
    }
    return false;
}

static u8 hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (u8)(ch - '0');
    }
    ch = ascii_lower(ch);
    return (ch >= 'a' && ch <= 'f') ? (u8)(10 + ch - 'a') : 0;
}

static bool parse_css_color(const char *value, Color *out) {
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    if (contains_text_ci(value, "transparent")) {
        return false;
    }
    if (*value == '#') {
        value++;
        if (strlen(value) >= 6) {
            *out = RGB((hex_value(value[0]) << 4) | hex_value(value[1]),
                       (hex_value(value[2]) << 4) | hex_value(value[3]),
                       (hex_value(value[4]) << 4) | hex_value(value[5]));
            return true;
        }
        if (strlen(value) >= 3) {
            u8 r = hex_value(value[0]);
            u8 g = hex_value(value[1]);
            u8 b = hex_value(value[2]);
            *out = RGB((r << 4) | r, (g << 4) | g, (b << 4) | b);
            return true;
        }
    }
    if (starts_with_ci(value, "rgb")) {
        i32 r = css_number(value);
        const char *comma = value;
        while (*comma && *comma != ',') {
            comma++;
        }
        i32 g = *comma ? css_number(comma + 1) : r;
        comma++;
        while (*comma && *comma != ',') {
            comma++;
        }
        i32 b = *comma ? css_number(comma + 1) : g;
        *out = RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
        return true;
    }
    if (starts_with_ci(value, "white")) { *out = RGB(255, 255, 255); return true; }
    if (starts_with_ci(value, "black")) { *out = RGB(0, 0, 0); return true; }
    if (starts_with_ci(value, "blue")) { *out = RGB(80, 150, 255); return true; }
    if (starts_with_ci(value, "purple")) { *out = RGB(168, 85, 247); return true; }
    if (starts_with_ci(value, "red")) { *out = RGB(239, 68, 68); return true; }
    if (starts_with_ci(value, "yellow")) { *out = RGB(234, 179, 8); return true; }
    if (starts_with_ci(value, "gray") || starts_with_ci(value, "grey")) { *out = RGB(107, 114, 128); return true; }
    return false;
}

static void web_apply_declarations(WebStyle *style, const char *css) {
    char value[80];
    Color color;
    if (copy_css_value(css, "color", value, sizeof(value)) && parse_css_color(value, &color)) {
        style->color = color;
    }
    if ((copy_css_value(css, "background-color", value, sizeof(value)) ||
         copy_css_value(css, "background", value, sizeof(value))) &&
        parse_css_color(value, &color)) {
        style->bg = color;
        style->has_bg = true;
    }
    if (copy_css_value(css, "border-radius", value, sizeof(value))) {
        style->radius = css_number(value);
    }
    if (copy_css_value(css, "padding", value, sizeof(value))) {
        style->padding = css_number(value);
    }
    if (copy_css_value(css, "margin", value, sizeof(value))) {
        style->margin = css_number(value);
    }
    if (copy_css_value(css, "width", value, sizeof(value))) {
        style->width = css_number(value);
    }
    if (copy_css_value(css, "height", value, sizeof(value))) {
        style->height = css_number(value);
    }
    if (copy_css_value(css, "font-size", value, sizeof(value))) {
        style->scale = css_number(value) >= 20 ? 2 : 1;
    }
    if (contains_text_ci(css, "backdrop-filter") || contains_text_ci(css, "box-shadow")) {
        style->glass = true;
    }
}

static bool copy_attr(const char *tag, const char *name, char *out, size_t out_size) {
    size_t name_len = strlen(name);
    for (const char *p = tag; *p; p++) {
        bool boundary = p == tag || p[-1] == ' ' || p[-1] == '\t';
        if (!boundary || !css_name_match(p, name)) {
            continue;
        }
        const char *q = p + name_len;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        if (*q != '=') {
            continue;
        }
        q++;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        char quote = (*q == '"' || *q == '\'') ? *q++ : ' ';
        size_t used = 0;
        while (*q && ((quote == ' ' && *q != ' ' && *q != '\t' && *q != '>') || (quote != ' ' && *q != quote)) &&
               used + 1 < out_size) {
            out[used++] = *q++;
        }
        out[used] = 0;
        return used > 0;
    }
    return false;
}

static void web_apply_embedded_css(const char *html, const char *selector, WebStyle *style) {
    const char *p = html;
    while ((p = find_after_ci(p, "<style")) && *p) {
        const char *end = find_after_ci(p, "</style>");
        while (p < end && *p) {
            if (css_name_match(p, selector)) {
                const char *open = p;
                while (open < end && *open != '{') {
                    open++;
                }
                const char *close = open;
                while (close < end && *close != '}') {
                    close++;
                }
                if (*open == '{') {
                    char decl[220];
                    size_t used = 0;
                    open++;
                    while (open < close && used + 1 < sizeof(decl)) {
                        decl[used++] = *open++;
                    }
                    decl[used] = 0;
                    web_apply_declarations(style, decl);
                }
                return;
            }
            p++;
        }
        p = end;
    }
}

static void web_default_style(const char *tag, WebStyle *style, Color inherited) {
    *style = (WebStyle){ inherited, RGB(24, 25, 32), false, false, 16, 8, 5, 0, 0, 1 };
    if (starts_with_ci(tag, "h1")) {
        style->scale = 2;
        style->color = RGB(248, 250, 252);
        style->margin = 10;
    } else if (starts_with_ci(tag, "h2") || starts_with_ci(tag, "h3")) {
        style->scale = 2;
        style->color = RGB(226, 232, 240);
    } else if (starts_with_ci(tag, "a")) {
        style->color = RGB(125, 181, 255);
    } else if (starts_with_ci(tag, "button") || starts_with_ci(tag, "input")) {
        style->has_bg = true;
        style->bg = RGB(59, 130, 246);
        style->color = RGB(255, 255, 255);
        style->radius = 16;
        style->padding = 12;
    }
}

static bool web_block_tag(const char *tag) {
    return starts_with_ci(tag, "div") || starts_with_ci(tag, "p") || starts_with_ci(tag, "li") ||
           starts_with_ci(tag, "h") || starts_with_ci(tag, "section") || starts_with_ci(tag, "article") ||
           starts_with_ci(tag, "main") || starts_with_ci(tag, "header") || starts_with_ci(tag, "footer") ||
           starts_with_ci(tag, "button") || starts_with_ci(tag, "input");
}

static void copy_tag_name(const char *tag, char *out, size_t out_size) {
    size_t used = 0;
    while (*tag == '<' || *tag == '/' || *tag == ' ' || *tag == '\t') {
        tag++;
    }
    while ((*tag >= 'a' && *tag <= 'z') || (*tag >= 'A' && *tag <= 'Z') || (*tag >= '0' && *tag <= '9')) {
        if (used + 1 < out_size) {
            out[used++] = *tag;
        }
        tag++;
    }
    out[used] = 0;
}

static i32 draw_web_text(i32 x, i32 y, i32 max_w, const char *text, const WebStyle *style) {
    i32 pad = (style->has_bg || style->glass) ? style->padding : 0;
    i32 box_w = style->width > 0 && style->width < max_w ? style->width : max_w;
    i32 text_w = box_w - pad * 2;
    i32 char_w = 6 * (i32)style->scale;
    i32 chars = text_w > char_w ? text_w / char_w : 12;
    i32 line_h = 12 * (i32)style->scale + 5;
    i32 len = (i32)strlen(text);
    i32 lines = len > 0 ? (len + chars - 1) / chars : 1;
    if (lines > 8) {
        lines = 8;
    }
    i32 box_h = style->height > 0 ? style->height : lines * line_h + pad * 2;
    if (style->glass) {
        gfx_liquid_glass_rect(x, y, box_w, box_h, style->radius);
    } else if (style->has_bg) {
        gfx_fill_round_rect_plain_alpha(x, y, box_w, box_h, style->radius, style->bg, 225);
    }
    i32 offset = 0;
    for (i32 line = 0; line < lines && text[offset]; line++) {
        char chunk[96];
        i32 used = 0;
        while (text[offset] && used < chars && used + 1 < (i32)sizeof(chunk)) {
            chunk[used++] = text[offset++];
        }
        chunk[used] = 0;
        gfx_draw_text(x + pad, y + pad + line * line_h, chunk, style->color, style->scale);
    }
    return box_h;
}

static void draw_plain_response(i32 panel_x, i32 panel_y, i32 panel_w, i32 panel_h, LiqueiaTab *tab) {
    gfx_fill_round_rect_plain_alpha(panel_x, panel_y, panel_w, panel_h, 24, RGB(24, 25, 32), 255);
    gfx_draw_text(panel_x + 28, panel_y + 24, tab->response.status == 200 ? "Page loaded" : "Request failed", RGB(246, 238, 222), 2);
    gfx_draw_text(panel_x + 28, panel_y + 56, tab->mime, RGB(216, 170, 88), 1);
    const char *line = tab->display_text;
    for (i32 row = 0; *line && row < 12; row++) {
        char text[90];
        size_t used = 0;
        while (line[used] && line[used] != '\n' && used + 1 < sizeof(text)) {
            text[used] = line[used];
            used++;
        }
        text[used] = 0;
        if (text[0]) {
            gfx_draw_text(panel_x + 28, panel_y + 94 + row * 22, text, RGB(205, 205, 212), 1);
        }
        line += used;
        if (*line == '\n') {
            line++;
        }
    }
}

static void draw_html_response(i32 panel_x, i32 panel_y, i32 panel_w, i32 panel_h, LiqueiaTab *tab) {
    const char *html = tab->body;
    WebStyle body = { RGB(226, 232, 240), RGB(17, 24, 39), true, false, 24, 10, 6, 0, 0, 1 };
    web_apply_embedded_css(html, "body", &body);
    const char *body_tag = find_after_ci(html, "<body");
    if (*body_tag) {
        char tag[220];
        size_t used = 0;
        while (*body_tag && *body_tag != '>' && used + 1 < sizeof(tag)) {
            tag[used++] = *body_tag++;
        }
        tag[used] = 0;
        char style_attr[180];
        if (copy_attr(tag, "style", style_attr, sizeof(style_attr))) {
            web_apply_declarations(&body, style_attr);
        }
    }

    gfx_fill_round_rect_plain_alpha(panel_x, panel_y, panel_w, panel_h, body.radius, body.bg, 238);
    gfx_set_clip(panel_x, panel_y, panel_w, panel_h);
    WebStyle stack[6];
    i32 sp = 0;
    WebStyle current = body;
    i32 cursor_y = panel_y + 24;
    i32 content_x = panel_x + 28;
    i32 content_w = panel_w - 56;
    const char *p = html;
    while (*p && cursor_y < panel_y + panel_h - 24) {
        if (starts_with_ci(p, "<script") || starts_with_ci(p, "<style") || starts_with_ci(p, "<svg")) {
            p = find_after_ci(p, starts_with_ci(p, "<svg") ? "</svg>" : (starts_with_ci(p, "<style") ? "</style>" : "</script>"));
            continue;
        }
        if (*p == '<') {
            char tag[240];
            size_t used = 0;
            const char *q = p;
            while (*q && *q != '>' && used + 1 < sizeof(tag)) {
                tag[used++] = *q++;
            }
            tag[used] = 0;
            p = *q == '>' ? q + 1 : q;
            if (tag[1] == '/') {
                if (sp > 0) {
                    current = stack[--sp];
                }
                cursor_y += 2;
                continue;
            }
            char name[18];
            copy_tag_name(tag, name, sizeof(name));
            if (!name[0] || starts_with_ci(name, "meta") || starts_with_ci(name, "link")) {
                continue;
            }
            if (starts_with_ci(name, "br")) {
                cursor_y += 18;
                continue;
            }
            WebStyle next;
            web_default_style(name, &next, current.color);
            web_apply_embedded_css(html, name, &next);
            char class_attr[32];
            if (copy_attr(tag, "class", class_attr, sizeof(class_attr))) {
                char selector[36] = ".";
                append_text(selector, sizeof(selector), class_attr);
                web_apply_embedded_css(html, selector, &next);
            }
            char style_attr[180];
            if (copy_attr(tag, "style", style_attr, sizeof(style_attr))) {
                web_apply_declarations(&next, style_attr);
            }
            if (web_block_tag(name)) {
                cursor_y += next.margin;
            }
            if ((next.glass || next.has_bg) && next.height > 0) {
                i32 bw = next.width > 0 && next.width < content_w ? next.width : content_w;
                i32 bx = content_x + (content_w - bw) / 2;
                if (next.glass) {
                    gfx_liquid_glass_rect(bx, cursor_y, bw, next.height, next.radius);
                } else {
                    gfx_fill_round_rect_plain_alpha(bx, cursor_y, bw, next.height, next.radius, next.bg, 225);
                }
                cursor_y += next.height + next.margin;
            }
            if (sp < (i32)(sizeof(stack) / sizeof(stack[0]))) {
                stack[sp++] = current;
            }
            current = next;
            continue;
        }

        char text[220];
        size_t used = 0;
        bool last_space = false;
        while (*p && *p != '<' && used + 1 < sizeof(text)) {
            if (*p == '&') {
                append_entity_char(text, sizeof(text), &used, &p);
                last_space = false;
                continue;
            }
            char ch = *p++;
            if (ch == '\r' || ch == '\n' || ch == '\t') {
                ch = ' ';
            }
            if (ch == ' ') {
                if (last_space || used == 0) {
                    continue;
                }
                last_space = true;
            } else {
                last_space = false;
            }
            text[used++] = ch;
        }
        text[used] = 0;
        char trimmed[220];
        copy_trimmed(text, trimmed, sizeof(trimmed));
        if (trimmed[0] && strlen(trimmed) > 1) {
            cursor_y += draw_web_text(content_x, cursor_y, content_w, trimmed, &current) + current.margin;
        }
    }
    gfx_clear_clip();
}

static void draw_web_page(i32 x, i32 y, i32 width, i32 height) {
    LiqueiaTab *tab = &tabs[active_tab];
    i32 panel_x = x + 24;
    i32 panel_y = y + 152;
    i32 panel_w = width - 48;
    i32 panel_h = height - 172;
    if (starts_with(tab->mime, "text/html")) {
        draw_html_response(panel_x, panel_y, panel_w, panel_h, tab);
    } else {
        draw_plain_response(panel_x, panel_y, panel_w, panel_h, tab);
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
