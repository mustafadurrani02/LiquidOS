#include <liquidos/app_store.h>
#include <liquidos/fs.h>
#include <liquidos/gfx.h>
#include <liquidos/loader.h>
#include <liquidos/lib.h>
#include <liquidos/pmm.h>
#include <liquidos/power.h>
#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/syscall.h>
#include <liquidos/terminal.h>

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
    terminal_write_line(line);
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

static void execute_command(const char *command) {
    char prompt_line[TERMINAL_LINE_LENGTH];
    prompt_line[0] = 0;
    append_text(prompt_line, sizeof(prompt_line), "> ");
    append_text(prompt_line, sizeof(prompt_line), command);
    terminal_write_line(prompt_line);

    if (strcmp(command, "help") == 0) {
        terminal_write_line("Commands: help, clear, about, mem, ps, spawn, runhello, runapps, syscall");
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
    } else if (strcmp(command, "ps") == 0) {
        for (size_t i = 0; i < process_count(); i++) {
            const Process *process = process_get(i);
            if (process) {
                terminal_write_process(process);
            }
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
    terminal_write_line("LiquidOS Terminal");
    terminal_write_line("Type help, ls, cat README.TXT, or write DESKTOP/HELLO.TXT hello.");
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
    Color bg = RGB(9, 13, 18);
    Color text = focused ? RGB(188, 242, 215) : RGB(164, 187, 195);
    Color muted = RGB(101, 122, 130);

    gfx_fill_rect(x, y, width, height, bg);

    i32 row_height = 20;
    i32 rows_available = (height - 34) / row_height;
    if (rows_available < 1) {
        rows_available = 1;
    }

    size_t start = 0;
    if (line_count > (size_t)rows_available) {
        start = line_count - (size_t)rows_available;
    }

    i32 draw_y = y + 8;
    for (size_t i = start; i < line_count; i++) {
        gfx_draw_text(x + 8, draw_y, lines[i], text, 1);
        draw_y += row_height;
    }

    gfx_fill_rect(x + 6, y + height - 28, width - 12, 1, RGB(34, 48, 55));
    gfx_draw_text(x + 8, y + height - 22, ">", muted, 1);
    gfx_draw_text(x + 24, y + height - 22, input, RGB(238, 245, 236), 1);
}
