#ifndef LIQUIDOS_SYSCALL_H
#define LIQUIDOS_SYSCALL_H

#include <liquidos/interrupts.h>
#include <liquidos/types.h>

typedef enum SyscallNumber {
    SYS_HELLO = 0,
    SYS_GETPID = 1,
    SYS_TICKS = 2,
    SYS_SPAWN_STUB = 3,
    SYS_INSTALL_APP = 4,
    SYS_COUNT
} SyscallNumber;

void syscall_init(void);
u64 syscall_dispatch(InterruptFrame *frame);
u64 syscall_call0(u64 number);
u64 syscall_call1(u64 number, u64 arg0);

#endif
