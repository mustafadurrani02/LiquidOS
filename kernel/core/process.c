#include <liquidos/lib.h>
#include <liquidos/process.h>
#include <liquidos/serial.h>

#define MAX_PROCESSES 16

static Process processes[MAX_PROCESSES];
static u32 next_pid = 1;
static u32 current_pid = 0;

static Process *allocate_process(void) {
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_UNUSED) {
            memset(&processes[i], 0, sizeof(processes[i]));
            processes[i].pid = next_pid++;
            return &processes[i];
        }
    }
    return NULL;
}

void process_init(void) {
    memset(processes, 0, sizeof(processes));
    next_pid = 1;
    current_pid = 0;
    process_spawn_kernel("kernel");
    process_spawn_kernel("desktop");
    serial_write_line("Process table initialized");
}

u32 process_spawn_kernel(const char *name) {
    Process *process = allocate_process();
    if (!process) {
        return 0;
    }
    strncpy(process->name, name, sizeof(process->name) - 1);
    process->name[sizeof(process->name) - 1] = 0;
    process->state = PROCESS_READY;
    process->mode = PROCESS_KERNEL;
    process->syscall_mask = ~0ULL;
    if (current_pid == 0) {
        current_pid = process->pid;
        process->state = PROCESS_RUNNING;
    }
    return process->pid;
}

u32 process_spawn_user_stub(const char *name, AddressSpace address_space, u64 entry_rip, u64 user_rsp, u64 user_base, u64 user_limit, u64 syscall_mask) {
    Process *process = allocate_process();
    if (!process) {
        return 0;
    }
    strncpy(process->name, name, sizeof(process->name) - 1);
    process->name[sizeof(process->name) - 1] = 0;
    process->state = PROCESS_READY;
    process->mode = PROCESS_USER;
    process->address_space = address_space;
    process->entry_rip = entry_rip;
    process->user_rsp = user_rsp;
    process->user_base = user_base;
    process->user_limit = user_limit;
    process->syscall_mask = syscall_mask;
    return process->pid;
}

bool process_exit_current(i32 code) {
    Process *process = process_current();
    if (!process || process->mode == PROCESS_KERNEL) {
        return false;
    }
    process->state = PROCESS_STOPPED;
    process->exit_code = code;
    process_set_current(1);
    return true;
}

size_t process_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_UNUSED) {
            count++;
        }
    }
    return count;
}

const Process *process_get(size_t index) {
    size_t current = 0;
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_UNUSED) {
            continue;
        }
        if (current++ == index) {
            return &processes[i];
        }
    }
    return NULL;
}

Process *process_current(void) {
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == current_pid) {
            return &processes[i];
        }
    }
    return NULL;
}

void process_set_current(u32 pid) {
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_RUNNING) {
            processes[i].state = PROCESS_READY;
        }
    }
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) {
            processes[i].state = PROCESS_RUNNING;
            current_pid = pid;
            return;
        }
    }
}
