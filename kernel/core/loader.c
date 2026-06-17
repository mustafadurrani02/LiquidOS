#include <liquidos/fs.h>
#include <liquidos/loader.h>
#include <liquidos/lib.h>
#include <liquidos/pmm.h>
#include <liquidos/process.h>
#include <liquidos/serial.h>
#include <liquidos/usermode.h>
#include <liquidos/vmm.h>

#define USER_BASE  0x0000000000400000ULL
#define USER_STACK 0x0000000000600000ULL
#define MAX_APP_BYTES (FS_CONTENT_LENGTH)
#define MAX_COOP_SWITCHES 64U

void loader_init(void) {
    serial_write_line("App loader initialized");
}

static LoadResult make_result(bool ok, u32 pid, const char *message) {
    LoadResult out;
    out.ok = ok;
    out.pid = pid;
    strncpy(out.message, message, sizeof(out.message) - 1);
    out.message[sizeof(out.message) - 1] = 0;
    return out;
}

static u64 align_up(u64 value, u64 alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static const char *app_name_from_path(const char *path) {
    const char *name = path;
    for (const char *cursor = path; cursor && *cursor; cursor++) {
        if (*cursor == '/') {
            name = cursor + 1;
        }
    }
    return name;
}

static bool range_valid(u64 offset, u64 size, u64 total) {
    return offset <= total && size <= total - offset;
}

static bool map_segment(AddressSpace *space, u64 virtual_base, const u8 *source, u64 size, u64 flags) {
    u64 mapped = 0;
    while (mapped < size) {
        void *page = pmm_alloc_page();
        if (!page) {
            return false;
        }

        u64 copy = size - mapped;
        if (copy > VMM_PAGE_SIZE) {
            copy = VMM_PAGE_SIZE;
        }
        if (source) {
            memcpy(page, source + mapped, (size_t)copy);
        }

        if (!vmm_map_page(space, virtual_base + mapped, (u64)(uintptr_t)page, flags)) {
            return false;
        }
        mapped += VMM_PAGE_SIZE;
    }
    return true;
}

static bool map_stack(AddressSpace *space, u64 stack_size) {
    u64 size = align_up(stack_size, VMM_PAGE_SIZE);
    if (size < VMM_PAGE_SIZE) {
        size = VMM_PAGE_SIZE;
    }
    if (size > VMM_PAGE_SIZE * 4) {
        return false;
    }

    for (u64 offset = 0; offset < size; offset += VMM_PAGE_SIZE) {
        void *page = pmm_alloc_page();
        if (!page) {
            return false;
        }
        u64 va = USER_STACK - size + offset;
        if (!vmm_map_page(space, va, (u64)(uintptr_t)page, VMM_WRITE | VMM_USER)) {
            return false;
        }
    }

    return vmm_map_guard_page(space, USER_STACK - size - VMM_PAGE_SIZE);
}

static bool validate_header(const FsFile *file, const LappHeader **header_out) {
    if (!file || file->size < sizeof(LappHeader)) {
        return false;
    }

    const LappHeader *header = (const LappHeader *)(const void *)file->contents;
    if (memcmp(header->magic, LAPP_MAGIC, 4) != 0 || header->version != LAPP_VERSION) {
        return false;
    }
    if (header->header_size < sizeof(LappHeader) || header->header_size > file->size) {
        return false;
    }
    if (header->text_size == 0 || header->text_size > MAX_APP_BYTES ||
        !range_valid(header->text_offset, header->text_size, file->size)) {
        return false;
    }
    if (header->entry_offset >= header->text_size) {
        return false;
    }
    if (header->data_size && !range_valid(header->data_offset, header->data_size, file->size)) {
        return false;
    }
    if (header->stack_size == 0 || header->stack_size > VMM_PAGE_SIZE * 4) {
        return false;
    }
    if (header->bss_size > VMM_PAGE_SIZE * 4) {
        return false;
    }

    *header_out = header;
    return true;
}

LoadResult loader_spawn_app(const char *path) {
    const FsFile *file = fs_find(path);
    if (!file) {
        return make_result(false, 0, "app file not found");
    }

    const LappHeader *header = NULL;
    if (!validate_header(file, &header)) {
        return make_result(false, 0, "bad LAPP header");
    }

    AddressSpace space = vmm_create_user_space();
    if (!space.pml4_phys) {
        return make_result(false, 0, "address space allocation failed");
    }

    const u8 *bytes = (const u8 *)(const void *)file->contents;
    u64 text_base = USER_BASE;
    u64 data_base = USER_BASE + align_up(header->text_size, VMM_PAGE_SIZE);
    u64 bss_base = data_base + align_up(header->data_size, VMM_PAGE_SIZE);

    if (!map_segment(&space, text_base, bytes + header->text_offset, header->text_size, VMM_USER)) {
        return make_result(false, 0, "text mapping failed");
    }
    if (header->data_size && !map_segment(&space, data_base, bytes + header->data_offset, header->data_size, VMM_WRITE | VMM_USER)) {
        return make_result(false, 0, "data mapping failed");
    }
    if (header->bss_size && !map_segment(&space, bss_base, NULL, header->bss_size, VMM_WRITE | VMM_USER)) {
        return make_result(false, 0, "bss mapping failed");
    }
    if (!map_stack(&space, header->stack_size)) {
        return make_result(false, 0, "stack mapping failed");
    }

    u32 pid = process_spawn_user_stub(app_name_from_path(path), space, USER_BASE + header->entry_offset,
                                      USER_STACK - 16, USER_BASE,
                                      align_up(header->text_size, VMM_PAGE_SIZE) +
                                      align_up(header->data_size, VMM_PAGE_SIZE) +
                                      align_up(header->bss_size, VMM_PAGE_SIZE),
                                      header->syscall_mask);
    if (!pid) {
        return make_result(false, 0, "process table full");
    }

    return make_result(true, pid, "app loaded");
}

static void run_process_once(Process *process) {
    if (!process || process->state != PROCESS_READY || process->mode != PROCESS_USER) {
        return;
    }

    process_set_current(process->pid);
    if (process->context.valid) {
        user_resume_context(process->address_space.pml4_phys, &process->context);
    } else {
        user_enter(process->address_space.pml4_phys, process->entry_rip, process->user_rsp);
    }
}

static void run_ready_users(void) {
    u32 last_pid = 0;
    for (u32 switches = 0; switches < MAX_COOP_SWITCHES && process_has_ready_user(); switches++) {
        Process *next = process_next_ready_user(last_pid);
        if (!next) {
            break;
        }
        last_pid = next->pid;
        run_process_once(next);
    }
    process_set_current(1);
}

LoadResult loader_load_app(const char *path) {
    LoadResult loaded = loader_spawn_app(path);
    if (!loaded.ok) {
        return loaded;
    }

    serial_write("loader: running ");
    serial_write_line(app_name_from_path(path));
    run_ready_users();

    Process *process = process_get_by_pid(loaded.pid);
    if (process && process->state == PROCESS_STOPPED) {
        return make_result(true, loaded.pid, "app exited");
    }
    if (process && process->state == PROCESS_CRASHED) {
        return make_result(false, loaded.pid, "app crashed");
    }
    return make_result(false, loaded.pid, "app did not exit");
}

LoadResult loader_run_apps(const char *left_path, const char *right_path) {
    LoadResult left = loader_spawn_app(left_path);
    if (!left.ok) {
        return left;
    }
    LoadResult right = loader_spawn_app(right_path);
    if (!right.ok) {
        return right;
    }

    serial_write_line("loader: starting cooperative app run");
    run_ready_users();

    Process *left_process = process_get_by_pid(left.pid);
    Process *right_process = process_get_by_pid(right.pid);
    if (left_process && right_process &&
        left_process->state == PROCESS_STOPPED && right_process->state == PROCESS_STOPPED) {
        return make_result(true, right.pid, "apps completed");
    }
    if ((left_process && left_process->state == PROCESS_CRASHED) ||
        (right_process && right_process->state == PROCESS_CRASHED)) {
        return make_result(false, right.pid, "app crashed");
    }
    return make_result(false, right.pid, "apps did not complete");
}
