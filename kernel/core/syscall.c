#include <liquidos/app_store.h>
#include <liquidos/fs.h>
#include <liquidos/gdt.h>
#include <liquidos/lib.h>
#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>
#include <liquidos/usermode.h>
#include <liquidos/vmm.h>

#define USER_WINDOW_MAX 8

typedef struct UserWindow {
    bool used;
    u32 id;
    u32 owner_pid;
    i32 x;
    i32 y;
    i32 width;
    i32 height;
    char title[32];
} UserWindow;

static UserWindow user_windows[USER_WINDOW_MAX];
static u32 next_window_id = 1;

void syscall_init(void) {
    memset(user_windows, 0, sizeof(user_windows));
    next_window_id = 1;
    serial_write_line("Syscall table initialized");
}

static UserWindow *find_user_window(u32 id, u32 owner_pid) {
    for (size_t i = 0; i < USER_WINDOW_MAX; i++) {
        if (user_windows[i].used && user_windows[i].id == id && user_windows[i].owner_pid == owner_pid) {
            return &user_windows[i];
        }
    }
    return NULL;
}

static u64 sys_window_create(const char *title, i32 width, i32 height) {
    if (width < 80) {
        width = 80;
    }
    if (height < 60) {
        height = 60;
    }
    if (width > 640) {
        width = 640;
    }
    if (height > 420) {
        height = 420;
    }

    u32 owner = scheduler_current_pid();
    for (size_t i = 0; i < USER_WINDOW_MAX; i++) {
        if (!user_windows[i].used) {
            user_windows[i].used = true;
            user_windows[i].id = next_window_id++;
            user_windows[i].owner_pid = owner;
            user_windows[i].x = 120;
            user_windows[i].y = 120;
            user_windows[i].width = width;
            user_windows[i].height = height;
            strncpy(user_windows[i].title, title ? title : "User App", sizeof(user_windows[i].title) - 1);
            user_windows[i].title[sizeof(user_windows[i].title) - 1] = 0;
            return user_windows[i].id;
        }
    }
    return 0;
}

u64 syscall_dispatch(InterruptFrame *frame) {
    if (!frame) {
        return (u64)-1;
    }

    Process *current = process_current();
    if (current && current->mode == PROCESS_USER) {
        if (frame->rax >= SYS_COUNT || ((current->syscall_mask & (1ULL << frame->rax)) == 0)) {
            return (u64)-1;
        }
    }

    switch (frame->rax) {
    case SYS_WRITE:
        if (frame->rbx) {
            serial_write((const char *)(uintptr_t)frame->rbx);
        }
        return 0;
    case SYS_EXIT:
        if (!process_exit_current((i32)frame->rbx)) {
            return 1;
        }
        if ((frame->cs & 3) == 3) {
            vmm_switch(vmm_kernel_space());
            user_return_to_kernel_now();
        }
        return 0;
    case SYS_YIELD:
        if ((frame->cs & 3) == 3 && process_yield_current()) {
            vmm_switch(vmm_kernel_space());
            user_return_to_kernel_now();
        }
        return 0;
    case SYS_GETPID:
        return scheduler_current_pid();
    case SYS_TICKS:
        return scheduler_ticks();
    case SYS_OPEN:
        return frame->rbx ? (u64)process_open_current((const char *)(uintptr_t)frame->rbx) : (u64)-1;
    case SYS_READ: {
        if (!frame->rcx || frame->rdx == 0) {
            return (u64)-1;
        }
        if (frame->rbx >= PROCESS_FIRST_FD && frame->rbx < PROCESS_FIRST_FD + PROCESS_MAX_FILES) {
            return (u64)process_read_current((i32)frame->rbx, (void *)(uintptr_t)frame->rcx, (size_t)frame->rdx);
        }
        const FsFile *file = frame->rbx ? fs_find((const char *)(uintptr_t)frame->rbx) : NULL;
        if (!file) {
            return (u64)-1;
        }
        size_t copy = file->size < frame->rdx ? file->size : (size_t)frame->rdx;
        memcpy((void *)(uintptr_t)frame->rcx, file->contents, copy);
        return copy;
    }
    case SYS_FILE_WRITE:
        if (frame->rbx >= PROCESS_FIRST_FD && frame->rbx < PROCESS_FIRST_FD + PROCESS_MAX_FILES) {
            return frame->rcx ? (u64)process_write_current((i32)frame->rbx, (const char *)(uintptr_t)frame->rcx) : (u64)-1;
        }
        return frame->rbx && frame->rcx && fs_write((const char *)(uintptr_t)frame->rbx, (const char *)(uintptr_t)frame->rcx) ? 0 : 1;
    case SYS_CLOSE:
        return process_close_current((i32)frame->rbx) ? 0 : 1;
    case SYS_SPAWN_STUB:
        return process_spawn_user_stub("user-stub", vmm_kernel_space(), 0, 0, 0x400000, 0x10000,
                                       (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD) |
                                       (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS));
    case SYS_INSTALL_APP:
        return app_store_install_by_index((size_t)frame->rbx) ? 0 : 1;
    case SYS_HELLO:
        serial_write_line("sys_hello");
        return 0x514C49515549444FULL;
    case SYS_WINDOW_CREATE:
        return sys_window_create((const char *)(uintptr_t)frame->rbx, (i32)frame->rcx, (i32)frame->rdx);
    case SYS_DRAW_TEXT:
    case SYS_DRAW_RECT:
        return find_user_window((u32)frame->rbx, scheduler_current_pid()) ? 0 : 1;
    case SYS_POLL_EVENT:
        return find_user_window((u32)frame->rbx, scheduler_current_pid()) ? 0 : 1;
    case SYS_WINDOW_CLOSE: {
        UserWindow *window = find_user_window((u32)frame->rbx, scheduler_current_pid());
        if (!window) {
            return 1;
        }
        memset(window, 0, sizeof(*window));
        return 0;
    }
    default:
        return (u64)-1;
    }
}

u64 syscall_call0(u64 number) {
    u64 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}

u64 syscall_call1(u64 number, u64 arg0) {
    u64 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(arg0) : "memory");
    return result;
}
