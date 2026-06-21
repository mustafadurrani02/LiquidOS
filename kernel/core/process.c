#include <liquidos/fs.h>
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

bool process_yield_current(void) {
    Process *process = process_current();
    if (!process || process->mode == PROCESS_KERNEL) {
        return false;
    }
    process->state = PROCESS_READY;
    process_set_current(1);
    return true;
}

bool process_preempt_current(void) {
    Process *process = process_current();
    if (!process || process->mode == PROCESS_KERNEL) {
        return false;
    }
    process->state = PROCESS_READY;
    process_set_current(1);
    return true;
}

bool process_crash_current(u64 vector, u64 error_code, u64 rip, u64 fault_address) {
    Process *process = process_current();
    if (!process || process->mode == PROCESS_KERNEL) {
        return false;
    }
    process->state = PROCESS_CRASHED;
    process->exit_code = -1;
    process->crash_vector = vector;
    process->crash_error = error_code;
    process->crash_rip = rip;
    process->crash_address = fault_address;
    process_set_current(1);
    return true;
}

bool process_kill(u32 pid, i32 code) {
    Process *process = process_get_by_pid(pid);
    if (!process || process->mode == PROCESS_KERNEL || process->state == PROCESS_STOPPED || process->state == PROCESS_CRASHED) {
        return false;
    }

    process->state = PROCESS_STOPPED;
    process->exit_code = code;
    if (process_current() == process) {
        process_set_current(1);
    }
    return true;
}

i32 process_open_current(const char *path) {
    Process *process = process_current();
    if (!process || !path || !fs_find(path)) {
        return -1;
    }

    for (size_t i = 0; i < PROCESS_MAX_FILES; i++) {
        if (!process->files[i].used) {
            process->files[i].used = true;
            process->files[i].offset = 0;
            strncpy(process->files[i].path, path, sizeof(process->files[i].path) - 1);
            process->files[i].path[sizeof(process->files[i].path) - 1] = 0;
            return (i32)(PROCESS_FIRST_FD + i);
        }
    }

    return -1;
}

i64 process_read_current(i32 fd, void *buffer, size_t buffer_size) {
    Process *process = process_current();
    if (!process || !buffer || buffer_size == 0 || fd < PROCESS_FIRST_FD) {
        return -1;
    }

    size_t index = (size_t)(fd - PROCESS_FIRST_FD);
    if (index >= PROCESS_MAX_FILES || !process->files[index].used) {
        return -1;
    }

    const FsFile *file = fs_find(process->files[index].path);
    if (!file) {
        return -1;
    }

    if (process->files[index].offset >= file->size) {
        return 0;
    }

    size_t available = (size_t)(file->size - process->files[index].offset);
    size_t copy = available;
    if (copy > buffer_size) {
        copy = buffer_size;
    }
    memcpy(buffer, file->contents + process->files[index].offset, copy);
    process->files[index].offset += copy;
    return (i64)copy;
}

i64 process_write_current(i32 fd, const char *contents) {
    Process *process = process_current();
    if (!process || !contents || fd < PROCESS_FIRST_FD) {
        return -1;
    }

    size_t index = (size_t)(fd - PROCESS_FIRST_FD);
    if (index >= PROCESS_MAX_FILES || !process->files[index].used) {
        return -1;
    }

    return fs_write(process->files[index].path, contents) ? (i64)strlen(contents) : -1;
}

bool process_close_current(i32 fd) {
    Process *process = process_current();
    if (!process || fd < PROCESS_FIRST_FD) {
        return false;
    }

    size_t index = (size_t)(fd - PROCESS_FIRST_FD);
    if (index >= PROCESS_MAX_FILES || !process->files[index].used) {
        return false;
    }

    memset(&process->files[index], 0, sizeof(process->files[index]));
    return true;
}

void process_save_interrupt_frame(const InterruptFrame *frame) {
    Process *process = process_current();
    if (!process || !frame) {
        return;
    }

    process->context.r15 = frame->r15;
    process->context.r14 = frame->r14;
    process->context.r13 = frame->r13;
    process->context.r12 = frame->r12;
    process->context.r11 = frame->r11;
    process->context.r10 = frame->r10;
    process->context.r9 = frame->r9;
    process->context.r8 = frame->r8;
    process->context.rsi = frame->rsi;
    process->context.rdi = frame->rdi;
    process->context.rbp = frame->rbp;
    process->context.rdx = frame->rdx;
    process->context.rcx = frame->rcx;
    process->context.rbx = frame->rbx;
    process->context.rax = frame->rax;
    process->context.rip = frame->rip;
    process->context.cs = frame->cs;
    process->context.rflags = frame->rflags;
    process->context.rsp = (frame->cs & 3) == 3 ? frame->rsp : 0;
    process->context.ss = (frame->cs & 3) == 3 ? frame->ss : 0;
    process->context.valid = true;
}

bool process_restore_interrupt_frame(InterruptFrame *frame, const Process *process) {
    if (!frame || !process || !process->context.valid) {
        return false;
    }

    frame->r15 = process->context.r15;
    frame->r14 = process->context.r14;
    frame->r13 = process->context.r13;
    frame->r12 = process->context.r12;
    frame->r11 = process->context.r11;
    frame->r10 = process->context.r10;
    frame->r9 = process->context.r9;
    frame->r8 = process->context.r8;
    frame->rsi = process->context.rsi;
    frame->rdi = process->context.rdi;
    frame->rbp = process->context.rbp;
    frame->rdx = process->context.rdx;
    frame->rcx = process->context.rcx;
    frame->rbx = process->context.rbx;
    frame->rax = process->context.rax;
    frame->rip = process->context.rip;
    frame->cs = process->context.cs;
    frame->rflags = process->context.rflags;
    frame->rsp = process->context.rsp;
    frame->ss = process->context.ss;
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

Process *process_get_by_pid(u32 pid) {
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROCESS_UNUSED) {
            return &processes[i];
        }
    }
    return NULL;
}

Process *process_next_ready_user(u32 after_pid) {
    Process *first = NULL;
    bool choose_next = after_pid == 0;

    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        Process *candidate = &processes[i];
        if (candidate->pid == after_pid) {
            choose_next = true;
            continue;
        }
        if (candidate->state != PROCESS_READY || candidate->mode != PROCESS_USER) {
            continue;
        }
        if (!first) {
            first = candidate;
        }
        if (choose_next) {
            return candidate;
        }
    }

    return first;
}

bool process_has_ready_user(void) {
    return process_next_ready_user(0) != NULL;
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
            if (processes[i].state == PROCESS_UNUSED || processes[i].state == PROCESS_STOPPED || processes[i].state == PROCESS_CRASHED) {
                return;
            }
            processes[i].state = PROCESS_RUNNING;
            current_pid = pid;
            return;
        }
    }
}
