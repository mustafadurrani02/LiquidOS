#ifndef LIQUIDOS_SCHEDULER_H
#define LIQUIDOS_SCHEDULER_H

#include <liquidos/types.h>

void scheduler_init(void);
void scheduler_tick(void);
u64 scheduler_ticks(void);
u32 scheduler_current_pid(void);

#endif
