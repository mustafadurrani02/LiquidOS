#include <liquidos/app_store.h>
#include <liquidos/fs.h>
#include <liquidos/lib.h>
#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>
#include <liquidos/vmm.h>

void syscall_init(void) {
    serial_write_line("Syscall table initialized");
}

u64 syscall_dispatch(InterruptFrame *frame) {
    if (!frame) {
        return (u64)-1;
    }

    switch (frame->rax) {
    case SYS_WRITE:
        if (frame->rbx) {
            serial_write((const char *)(uintptr_t)frame->rbx);
        }
        return 0;
    case SYS_EXIT:
        return process_exit_current((i32)frame->rbx) ? 0 : 1;
    case SYS_YIELD:
        scheduler_tick();
        return 0;
    case SYS_GETPID:
        return scheduler_current_pid();
    case SYS_TICKS:
        return scheduler_ticks();
    case SYS_OPEN:
        return frame->rbx && fs_find((const char *)(uintptr_t)frame->rbx) ? 1 : 0;
    case SYS_READ: {
        const FsFile *file = frame->rbx ? fs_find((const char *)(uintptr_t)frame->rbx) : NULL;
        if (!file || !frame->rcx || frame->rdx == 0) {
            return 0;
        }
        size_t copy = file->size;
        if (copy + 1 > frame->rdx) {
            copy = frame->rdx - 1;
        }
        memcpy((void *)(uintptr_t)frame->rcx, file->contents, copy);
        ((char *)(uintptr_t)frame->rcx)[copy] = 0;
        return copy;
    }
    case SYS_FILE_WRITE:
        return frame->rbx && frame->rcx && fs_write((const char *)(uintptr_t)frame->rbx, (const char *)(uintptr_t)frame->rcx) ? 0 : 1;
    case SYS_CLOSE:
        return 0;
    case SYS_SPAWN_STUB:
        return process_spawn_user_stub("user-stub", vmm_kernel_space(), 0, 0, 0x400000, 0x10000,
                                       (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD) |
                                       (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS));
    case SYS_INSTALL_APP:
        return app_store_install_by_index((size_t)frame->rbx) ? 0 : 1;
    case SYS_HELLO:
        serial_write_line("sys_hello");
        return 0x514C49515549444FULL;
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
