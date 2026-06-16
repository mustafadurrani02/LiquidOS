#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/serial.h>

static u64 ticks = 0;

void scheduler_init(void) {
    ticks = 0;
    serial_write_line("Scheduler initialized");
}

void scheduler_tick(void) {
    ticks++;

    Process *current = process_current();
    if (current) {
        current->ticks++;
    }

    size_t count = process_count();
    if (count < 2 || (ticks % 5) != 0) {
        return;
    }

    u32 current_pid = current ? current->pid : 0;
    bool choose_next = current_pid == 0;
    const Process *first_ready = NULL;

    for (size_t i = 0; i < count; i++) {
        const Process *candidate = process_get(i);
        if (!candidate || candidate->state == PROCESS_STOPPED || candidate->state == PROCESS_SLEEPING) {
            continue;
        }
        if (!first_ready) {
            first_ready = candidate;
        }
        if (choose_next) {
            process_set_current(candidate->pid);
            return;
        }
        if (candidate->pid == current_pid) {
            choose_next = true;
        }
    }

    if (first_ready) {
        process_set_current(first_ready->pid);
    }
}

u64 scheduler_ticks(void) {
    return ticks;
}

u32 scheduler_current_pid(void) {
    Process *current = process_current();
    return current ? current->pid : 0;
}
