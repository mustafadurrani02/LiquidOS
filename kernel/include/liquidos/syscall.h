#ifndef LIQUIDOS_SYSCALL_H
#define LIQUIDOS_SYSCALL_H

#include <liquidos/interrupts.h>
#include <liquidos/types.h>

typedef enum SyscallNumber {
    SYS_WRITE = 0,
    SYS_EXIT = 1,
    SYS_YIELD = 2,
    SYS_GETPID = 3,
    SYS_TICKS = 4,
    SYS_OPEN = 5,
    SYS_READ = 6,
    SYS_FILE_WRITE = 7,
    SYS_CLOSE = 8,
    SYS_SPAWN_STUB = 9,
    SYS_INSTALL_APP = 10,
    SYS_HELLO = 11,
    SYS_WINDOW_CREATE = 12,
    SYS_DRAW_TEXT = 13,
    SYS_DRAW_RECT = 14,
    SYS_POLL_EVENT = 15,
    SYS_WINDOW_CLOSE = 16,
    SYS_COUNT
} SyscallNumber;

void syscall_init(void);
u64 syscall_dispatch(InterruptFrame *frame);
u64 syscall_call0(u64 number);
u64 syscall_call1(u64 number, u64 arg0);

#endif
