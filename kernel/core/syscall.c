#include <liquidos/app_store.h>
#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>

void syscall_init(void) {
    serial_write_line("Syscall table initialized");
}

u64 syscall_dispatch(InterruptFrame *frame) {
    if (!frame) {
        return (u64)-1;
    }

    switch (frame->rax) {
    case SYS_HELLO:
        serial_write_line("sys_hello");
        return 0x514C49515549444FULL;
    case SYS_GETPID:
        return scheduler_current_pid();
    case SYS_TICKS:
        return scheduler_ticks();
    case SYS_SPAWN_STUB:
        return process_spawn_user_stub("user-stub", 0x400000, 0x10000, (1ULL << SYS_HELLO) | (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS));
    case SYS_INSTALL_APP:
        return app_store_install_by_index((size_t)frame->rbx) ? 0 : 1;
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
