#ifndef LIQUIDOS_PROCESS_H
#define LIQUIDOS_PROCESS_H

#include <liquidos/types.h>
#include <liquidos/interrupts.h>
#include <liquidos/vmm.h>

#define PROCESS_MAX_FILES 8
#define PROCESS_FIRST_FD  3

typedef struct ProcessFile {
    bool used;
    char path[40];
    u64 offset;
} ProcessFile;

typedef struct ProcessContext {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
    bool valid;
} ProcessContext;

typedef enum ProcessState {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_STOPPED,
    PROCESS_CRASHED
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
    u64 crash_vector;
    u64 crash_error;
    u64 crash_rip;
    u64 crash_address;
    AddressSpace address_space;
    u64 entry_rip;
    u64 user_rsp;
    i32 exit_code;
    ProcessFile files[PROCESS_MAX_FILES];
    ProcessContext context;
} Process;

void process_init(void);
u32 process_spawn_kernel(const char *name);
u32 process_spawn_user_stub(const char *name, AddressSpace address_space, u64 entry_rip, u64 user_rsp, u64 user_base, u64 user_limit, u64 syscall_mask);
bool process_exit_current(i32 code);
bool process_yield_current(void);
bool process_preempt_current(void);
bool process_crash_current(u64 vector, u64 error_code, u64 rip, u64 fault_address);
bool process_kill(u32 pid, i32 code);
i32 process_open_current(const char *path);
i64 process_read_current(i32 fd, void *buffer, size_t buffer_size);
i64 process_write_current(i32 fd, const char *contents);
bool process_close_current(i32 fd);
void process_save_interrupt_frame(const InterruptFrame *frame);
bool process_restore_interrupt_frame(InterruptFrame *frame, const Process *process);
size_t process_count(void);
const Process *process_get(size_t index);
Process *process_get_by_pid(u32 pid);
Process *process_next_ready_user(u32 after_pid);
bool process_has_ready_user(void);
Process *process_current(void);
void process_set_current(u32 pid);

#endif
