#ifndef LIQUIDOS_PROCESS_H
#define LIQUIDOS_PROCESS_H

#include <liquidos/types.h>

typedef enum ProcessState {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_STOPPED
} ProcessState;

typedef enum ProcessMode {
    PROCESS_KERNEL = 0,
    PROCESS_USER
} ProcessMode;

typedef struct Process {
    u32 pid;
    char name[24];
    ProcessState state;
    ProcessMode mode;
    u64 ticks;
    u64 user_base;
    u64 user_limit;
    u64 syscall_mask;
} Process;

void process_init(void);
u32 process_spawn_kernel(const char *name);
u32 process_spawn_user_stub(const char *name, u64 user_base, u64 user_limit, u64 syscall_mask);
size_t process_count(void);
const Process *process_get(size_t index);
Process *process_current(void);
void process_set_current(u32 pid);

#endif
