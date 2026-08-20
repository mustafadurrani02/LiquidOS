#include <liquidos/app_store.h>
#include <liquidos/fs.h>
#include <liquidos/gfx.h>
#include <liquidos/input.h>
#include <liquidos/loader.h>
#include <liquidos/lib.h>
#include <liquidos/network.h>
#include <liquidos/platform.h>
#include <liquidos/pmm.h>
#include <liquidos/power.h>
#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/syscall.h>
#include <liquidos/terminal.h>
#include <liquidos/ui.h>

#define TERMINAL_MAX_LINES 48
#define TERMINAL_LINE_LENGTH 96
#define TERMINAL_INPUT_LENGTH 80

static char lines[TERMINAL_MAX_LINES][TERMINAL_LINE_LENGTH];
static size_t line_count = 0;
static char input[TERMINAL_INPUT_LENGTH];
static size_t input_length = 0;

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used = strlen(dest);
    while (*src && used + 1 < dest_size) {
        dest[used++] = *src++;
    }
    dest[used] = 0;
}

static void terminal_write_line(const char *text) {
    if (line_count >= TERMINAL_MAX_LINES) {
        for (size_t i = 1; i < TERMINAL_MAX_LINES; i++) {
            strcpy(lines[i - 1], lines[i]);
        }
        line_count = TERMINAL_MAX_LINES - 1;
    }

    strncpy(lines[line_count], text, TERMINAL_LINE_LENGTH - 1);
    lines[line_count][TERMINAL_LINE_LENGTH - 1] = 0;
    line_count++;
}

static void terminal_write_kib_line(const char *label, u64 bytes) {
    char number[32];
    char line[TERMINAL_LINE_LENGTH];

    u64_to_dec(bytes / 1024, number, sizeof(number));
    line[0] = 0;
    append_text(line, sizeof(line), label);
    append_text(line, sizeof(line), number);
    append_text(line, sizeof(line), " KiB");
    terminal_write_line(line);
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

static void terminal_write_process(const Process *process) {
    char pid[16];
    char ticks[24];
    char line[TERMINAL_LINE_LENGTH];

    u64_to_dec(process->pid, pid, sizeof(pid));
    u64_to_dec(process->ticks, ticks, sizeof(ticks));
    line[0] = 0;
    append_text(line, sizeof(line), pid);
    append_text(line, sizeof(line), " ");
    append_text(line, sizeof(line), process->mode == PROCESS_USER ? "user " : "kern ");
    append_text(line, sizeof(line), process_state_name(process->state));
    append_text(line, sizeof(line), " ");
    append_text(line, sizeof(line), process->name);
    append_text(line, sizeof(line), " ticks=");
    append_text(line, sizeof(line), ticks);
    append_text(line, sizeof(line), " sys=");
    u64_to_dec(process->syscalls, ticks, sizeof(ticks));
    append_text(line, sizeof(line), ticks);
    if (process->denied_syscalls) {
        append_text(line, sizeof(line), " denied=");
        u64_to_dec(process->denied_syscalls, ticks, sizeof(ticks));
        append_text(line, sizeof(line), ticks);
    }
    terminal_write_line(line);
    if (process->state == PROCESS_CRASHED) {
        char value[24];
        line[0] = 0;
        append_text(line, sizeof(line), "  crash vector=");
        u64_to_dec(process->crash_vector, value, sizeof(value));
        append_text(line, sizeof(line), value);
        append_text(line, sizeof(line), " rip=");
        u64_to_hex(process->crash_rip, value, sizeof(value));
        append_text(line, sizeof(line), value);
        terminal_write_line(line);
    }
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ') {
        text++;
    }
    return text;
}

static const char *copy_token(const char *text, char *out, size_t out_size) {
    size_t used = 0;
    text = skip_spaces(text);
    while (*text && *text != ' ' && used + 1 < out_size) {
        out[used++] = *text++;
    }
    out[used] = 0;
    return skip_spaces(text);
}

static bool starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void terminal_write_platform_line(const PlatformCapability *capability) {
    if (!capability) {
        return;
    }

    char line[TERMINAL_LINE_LENGTH];
    line[0] = 0;
    append_text(line, sizeof(line), capability->area);
    append_text(line, sizeof(line), ": ");
    append_text(line, sizeof(line), capability->name);
    append_text(line, sizeof(line), " [");
    append_text(line, sizeof(line), platform_status_name(capability->status));
    append_text(line, sizeof(line), "]");
    terminal_write_line(line);
}

static void terminal_write_platform_area(const char *area) {
    for (size_t i = 0; i < platform_capability_count(); i++) {
        const PlatformCapability *capability = platform_capability_get(i);
        if (capability && strcmp(capability->area, area) == 0) {
            terminal_write_platform_line(capability);
        }
    }
}

static void execute_command(const char *command) {
    char prompt_line[TERMINAL_LINE_LENGTH];
    prompt_line[0] = 0;
    append_text(prompt_line, sizeof(prompt_line), "> ");
    append_text(prompt_line, sizeof(prompt_line), command);
    terminal_write_line(prompt_line);

    if (strcmp(command, "help") == 0) {
        terminal_write_line("Commands: help, clear, about, mem, perf, ps, platform, drivers, services, security, privacy");
        terminal_write_line("Network: net, ping HOST, fetch URL PATH.");
        terminal_write_line("Apps: spawn, runhello, runapps, runcrash, syscall, apps, download NAME, runapp NAME, uninstall NAME.");
        terminal_write_line("Files: ls, cat, touch, write, rm. Store: apps, download NAME, runapp NAME, uninstall NAME.");
    } else if (strcmp(command, "clear") == 0) {
        line_count = 0;
    } else if (strcmp(command, "about") == 0) {
        terminal_write_line("LiquidOS: original x86_64 teaching OS.");
        terminal_write_line("Running in long mode with a glass desktop and RAM filesystem.");
    } else if (strcmp(command, "mem") == 0) {
        terminal_write_kib_line("Total memory: ", pmm_total_bytes());
        terminal_write_kib_line("Usable memory: ", pmm_usable_bytes());
        terminal_write_kib_line("Heap used: ", pmm_heap_used_bytes());
        terminal_write_kib_line("Heap limit: ", pmm_heap_limit_bytes());
        char pages[24];
        u64_to_dec(pmm_free_page_count(), pages, sizeof(pages));
        char line[TERMINAL_LINE_LENGTH];
        line[0] = 0;
        append_text(line, sizeof(line), "Free page frames: ");
        append_text(line, sizeof(line), pages);
        terminal_write_line(line);
    } else if (strcmp(command, "perf") == 0) {
        UiPerformanceStats stats = ui_performance_stats();
        char number[24];
        char line[TERMINAL_LINE_LENGTH];
        u64_to_dec(stats.render_calls, number, sizeof(number));
        line[0] = 0;
        append_text(line, sizeof(line), "Render calls: ");
        append_text(line, sizeof(line), number);
        append_text(line, sizeof(line), " frames=");
        u64_to_dec(stats.presented_frames, number, sizeof(number));
        append_text(line, sizeof(line), number);
        append_text(line, sizeof(line), " cursor=");
        u64_to_dec(stats.cursor_presents, number, sizeof(number));
        append_text(line, sizeof(line), number);
        terminal_write_line(line);
        line[0] = 0;
        append_text(line, sizeof(line), "Input: coalesced=");
        u64_to_dec(input_queue_coalesced_count(), number, sizeof(number));
        append_text(line, sizeof(line), number);
        append_text(line, sizeof(line), " dropped=");
        u64_to_dec(input_queue_dropped_count(), number, sizeof(number));
        append_text(line, sizeof(line), number);
        terminal_write_line(line);
        line[0] = 0;
        append_text(line, sizeof(line), "Dirty pixels presented: ");
        u64_to_dec(stats.dirty_pixels, number, sizeof(number));
        append_text(line, sizeof(line), number);
        terminal_write_line(line);
    } else if (strcmp(command, "ps") == 0) {
        for (size_t i = 0; i < process_count(); i++) {
            const Process *process = process_get(i);
            if (process) {
                terminal_write_process(process);
            }
        }
    } else if (strcmp(command, "platform") == 0) {
        PlatformSummary summary = platform_summary();
        char number[16];
        char line[TERMINAL_LINE_LENGTH];
        line[0] = 0;
        append_text(line, sizeof(line), "Platform: ");
        u64_to_dec(summary.available, number, sizeof(number));
        append_text(line, sizeof(line), number);
        append_text(line, sizeof(line), " available, ");
        u64_to_dec(summary.partial, number, sizeof(number));
        append_text(line, sizeof(line), number);
        append_text(line, sizeof(line), " partial, ");
        u64_to_dec(summary.planned, number, sizeof(number));
        append_text(line, sizeof(line), number);
        append_text(line, sizeof(line), " planned, ");
        u64_to_dec(summary.missing, number, sizeof(number));
        append_text(line, sizeof(line), number);
        append_text(line, sizeof(line), " missing");
        terminal_write_line(line);
        for (size_t i = 0; i < platform_capability_count(); i++) {
            terminal_write_platform_line(platform_capability_get(i));
        }
    } else if (strcmp(command, "drivers") == 0) {
        terminal_write_platform_area("hardware");
        terminal_write_platform_area("networking");
    } else if (strcmp(command, "services") == 0) {
        terminal_write_platform_area("services");
        terminal_write_platform_area("tooling");
        terminal_write_platform_area("recovery");
    } else if (strcmp(command, "security") == 0) {
        terminal_write_platform_area("isolation");
        terminal_write_platform_area("security");
    } else if (strcmp(command, "privacy") == 0) {
        terminal_write_line("Privacy mode: user apps cannot read SYSTEM, STORE, or NET.");
        terminal_write_line("Writes are limited to Home, Desktop, Documents, Downloads, media folders.");
        terminal_write_line("Syscall masks are enforced per package and denied calls are counted.");
        terminal_write_line("App file handles close automatically on exit, crash, or force close.");
    } else if (strcmp(command, "net") == 0) {
        const NetInfo *info = network_info();
        char line[TERMINAL_LINE_LENGTH];
        line[0] = 0;
        append_text(line, sizeof(line), "Driver: ");
        append_text(line, sizeof(line), info->driver);
        terminal_write_line(line);
        line[0] = 0;
        append_text(line, sizeof(line), "IPv4: ");
        append_text(line, sizeof(line), info->ipv4);
        append_text(line, sizeof(line), " gateway ");
        append_text(line, sizeof(line), info->gateway);
        terminal_write_line(line);
        line[0] = 0;
        append_text(line, sizeof(line), "DNS: ");
        append_text(line, sizeof(line), info->dns);
        append_text(line, sizeof(line), info->link_up ? " link up" : " link down");
        terminal_write_line(line);
    } else if (starts_with(command, "ping ")) {
        char host[NET_HOST_LENGTH];
        copy_token(command + 5, host, sizeof(host));
        terminal_write_line(network_ping(host) ? "ping: reply received" : "ping: host unreachable");
    } else if (starts_with(command, "fetch ")) {
        char url[NET_URL_LENGTH];
        char path[FS_NAME_LENGTH];
        const char *rest = copy_token(command + 6, url, sizeof(url));
        copy_token(rest, path, sizeof(path));
        if (!path[0]) {
            terminal_write_line("Usage: fetch URL PATH");
        } else if (network_download_to_file(url, path)) {
            terminal_write_line("Downloaded file saved.");
        } else {
            NetResponse fetched = network_fetch(url);
            terminal_write_line(fetched.message);
        }
    } else if (strcmp(command, "spawn") == 0) {
        u64 pid = syscall_call0(SYS_SPAWN_STUB);
        char pid_text[24];
        u64_to_dec(pid, pid_text, sizeof(pid_text));
        char line[TERMINAL_LINE_LENGTH];
        line[0] = 0;
        append_text(line, sizeof(line), pid ? "Spawned user stub pid " : "Could not spawn user stub ");
        append_text(line, sizeof(line), pid_text);
        terminal_write_line(line);
    } else if (strcmp(command, "runhello") == 0) {
        LoadResult loaded = loader_load_app("APPS/HELLO.APP");
        char pid_text[24];
        char line[TERMINAL_LINE_LENGTH];
        u64_to_dec(loaded.pid, pid_text, sizeof(pid_text));
        line[0] = 0;
        append_text(line, sizeof(line), loaded.ok ? "Loaded hello.app pid " : "Could not load hello.app: ");
        append_text(line, sizeof(line), loaded.ok ? pid_text : loaded.message);
        terminal_write_line(line);
    } else if (strcmp(command, "runapps") == 0) {
        LoadResult loaded = loader_run_apps("APPS/APP_A.APP", "APPS/APP_B.APP");
        terminal_write_line(loaded.ok ? "Cooperative apps completed." : loaded.message);
    } else if (strcmp(command, "runcrash") == 0) {
        LoadResult loaded = loader_load_app("APPS/CRASH.APP");
        terminal_write_line(loaded.ok ? "Unexpected crash app success." : loaded.message);
    } else if (strcmp(command, "syscall") == 0) {
        char value[32];
        char line[TERMINAL_LINE_LENGTH];
        u64_to_hex(syscall_call0(SYS_HELLO), value, sizeof(value));
        line[0] = 0;
        append_text(line, sizeof(line), "SYS_HELLO -> ");
        append_text(line, sizeof(line), value);
        terminal_write_line(line);
        u64_to_dec(syscall_call0(SYS_GETPID), value, sizeof(value));
        line[0] = 0;
        append_text(line, sizeof(line), "SYS_GETPID -> ");
        append_text(line, sizeof(line), value);
        terminal_write_line(line);
    } else if (strcmp(command, "apps") == 0) {
        for (size_t i = 0; i < app_store_count(); i++) {
            const StoreApp *app = app_store_get(i);
            if (app) {
                char line[TERMINAL_LINE_LENGTH];
                line[0] = 0;
                append_text(line, sizeof(line), app->name);
                append_text(line, sizeof(line), app_store_is_installed(i) ? " [installed] - " : " - ");
                append_text(line, sizeof(line), app->description);
                terminal_write_line(line);
            }
        }
    } else if (starts_with(command, "download ")) {
        char name[FS_NAME_LENGTH];
        copy_token(command + 9, name, sizeof(name));
        terminal_write_line(app_store_install(name) ? "App package installed." : "App install failed.");
    } else if (starts_with(command, "runapp ")) {
        char name[FS_NAME_LENGTH];
        copy_token(command + 7, name, sizeof(name));
        LoadResult loaded = app_store_launch(name);
        if (loaded.ok) {
            char pid_text[24];
            char line[TERMINAL_LINE_LENGTH];
            u64_to_dec(loaded.pid, pid_text, sizeof(pid_text));
            line[0] = 0;
            append_text(line, sizeof(line), "App completed pid ");
            append_text(line, sizeof(line), pid_text);
            terminal_write_line(line);
        } else {
            terminal_write_line(loaded.message);
        }
    } else if (starts_with(command, "uninstall ")) {
        char name[FS_NAME_LENGTH];
        copy_token(command + 10, name, sizeof(name));
        terminal_write_line(app_store_uninstall(name) ? "App removed." : "Installed app not found.");
    } else if (strcmp(command, "ls") == 0) {
        for (size_t i = 0; i < fs_file_count(); i++) {
            const FsFile *file = fs_get_file(i);
            if (file) {
                char size_text[24];
                char line[TERMINAL_LINE_LENGTH];
                u64_to_dec(file->size, size_text, sizeof(size_text));
                line[0] = 0;
                append_text(line, sizeof(line), file->name);
                append_text(line, sizeof(line), "  ");
                append_text(line, sizeof(line), size_text);
                append_text(line, sizeof(line), " bytes");
                terminal_write_line(line);
            }
        }
    } else if (starts_with(command, "cat ")) {
        char name[FS_NAME_LENGTH];
        copy_token(command + 4, name, sizeof(name));
        const FsFile *file = fs_find(name);
        if (!file) {
            terminal_write_line("File not found.");
        } else if (file->contents[0] == 0) {
            terminal_write_line("(empty)");
        } else {
            terminal_write_line(file->contents);
        }
    } else if (starts_with(command, "touch ")) {
        char name[FS_NAME_LENGTH];
        copy_token(command + 6, name, sizeof(name));
        terminal_write_line(fs_create(name) ? "File created." : "Could not create file.");
    } else if (starts_with(command, "write ")) {
        char name[FS_NAME_LENGTH];
        const char *contents = copy_token(command + 6, name, sizeof(name));
        terminal_write_line(fs_write(name, contents) ? "File saved." : "Could not save file.");
    } else if (starts_with(command, "rm ")) {
        char name[FS_NAME_LENGTH];
        copy_token(command + 3, name, sizeof(name));
        terminal_write_line(fs_delete(name) ? "File deleted." : "Could not delete file.");
    } else if (strcmp(command, "reboot") == 0) {
        terminal_write_line("Rebooting...");
        power_reboot();
    } else if (strcmp(command, "shutdown") == 0) {
        terminal_write_line("Sending VirtualBox shutdown ports...");
        power_shutdown();
    } else if (command[0] == 0) {
        terminal_write_line("");
    } else {
        terminal_write_line("Unknown command. Type help.");
    }
}

void terminal_init(void) {
    line_count = 0;
    input_length = 0;
    input[0] = 0;
    terminal_write_line("LiquidOS Terminal ready.");
    terminal_write_line("Type help, perf, privacy, ls, or cat README.TXT.");
}

void terminal_on_char(char ch) {
    if (ch == '\b') {
        if (input_length > 0) {
            input[--input_length] = 0;
        }
        return;
    }

    if (ch == '\n') {
        execute_command(input);
        input_length = 0;
        input[0] = 0;
        return;
    }

    if (ch == '\t') {
        ch = ' ';
    }

    if (ch >= 32 && ch <= 126 && input_length + 1 < TERMINAL_INPUT_LENGTH) {
        input[input_length++] = ch;
        input[input_length] = 0;
    }
}

void terminal_render(i32 x, i32 y, i32 width, i32 height, bool focused) {
    Color text = focused ? RGB(218, 248, 236) : RGB(172, 191, 199);
    Color muted = RGB(116, 134, 144);
    Color prompt = focused ? RGB(145, 244, 196) : RGB(118, 188, 166);
    Color command = RGB(255, 217, 136);

    gfx_fill_round_rect_plain_alpha(x, y, width, height, 18, RGB(5, 8, 14), 246);
    gfx_fill_round_rect_plain_alpha(x + 8, y + 8, width - 16, height - 16, 16, RGB(15, 20, 31), 196);
    gfx_liquid_glass_rect(x + 10, y + 10, width - 20, 38, 14);
    gfx_fill_round_rect_alpha(x + 10, y + 10, width - 20, 38, 14, RGB(35, 46, 62), focused ? 104 : 72);
    gfx_fill_round_rect_alpha(x + 22, y + 20, 10, 10, 5, RGB(255, 94, 88), 235);
    gfx_fill_round_rect_alpha(x + 38, y + 20, 10, 10, 5, RGB(255, 194, 66), 235);
    gfx_fill_round_rect_alpha(x + 54, y + 20, 10, 10, 5, RGB(45, 214, 104), 235);
    gfx_draw_text(x + 76, y + 21, "Terminal", RGB(238, 244, 248), 1);
    gfx_fill_round_rect_alpha(x + width - 138, y + 17, 112, 24, 10, focused ? RGB(70, 105, 92) : RGB(42, 50, 60), 116);
    gfx_draw_text(x + width - 122, y + 25, focused ? "SECURE SHELL" : "BACKGROUND", focused ? RGB(190, 246, 218) : RGB(150, 162, 172), 1);

    i32 row_height = 20;
    i32 output_top = y + 62;
    i32 prompt_h = 42;
    i32 rows_available = (height - 82 - prompt_h) / row_height;
    if (rows_available < 1) {
        rows_available = 1;
    }

    size_t start = 0;
    if (line_count > (size_t)rows_available) {
        start = line_count - (size_t)rows_available;
    }

    i32 draw_y = output_top;
    for (size_t i = start; i < line_count; i++) {
        Color line_color = text;
        if (lines[i][0] == '>') {
            line_color = command;
        } else if (starts_with(lines[i], "Privacy") || starts_with(lines[i], "Syscall") || starts_with(lines[i], "Render") || starts_with(lines[i], "Input")) {
            line_color = RGB(168, 218, 255);
        } else if (starts_with(lines[i], "Unknown") || starts_with(lines[i], "Could not") || starts_with(lines[i], "App install failed")) {
            line_color = RGB(255, 156, 156);
        }
        gfx_draw_text(x + 24, draw_y, lines[i], line_color, 1);
        draw_y += row_height;
    }

    i32 prompt_y = y + height - prompt_h - 12;
    gfx_liquid_glass_rect(x + 14, prompt_y, width - 28, prompt_h, 16);
    gfx_fill_round_rect_alpha(x + 14, prompt_y, width - 28, prompt_h, 16, RGB(255, 255, 255), focused ? 34 : 22);
    gfx_draw_text(x + 28, prompt_y + 15, "liquidos", prompt, 1);
    gfx_draw_text(x + 78, prompt_y + 15, ">", muted, 1);
    gfx_draw_text(x + 96, prompt_y + 15, input, RGB(244, 248, 246), 1);
    if (focused && ((scheduler_ticks() / 45) % 2) == 0) {
        i32 cursor_x = x + 100 + (i32)strlen(input) * 7;
        gfx_fill_round_rect_alpha(cursor_x, prompt_y + 12, 2, 16, 1, RGB(180, 248, 216), 210);
    }
}
