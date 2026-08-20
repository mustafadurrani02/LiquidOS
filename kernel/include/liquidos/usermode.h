#ifndef LIQUIDOS_USERMODE_H
#define LIQUIDOS_USERMODE_H

#include <liquidos/types.h>
#include <liquidos/process.h>

void user_enter(u64 pml4_phys, u64 entry_rip, u64 user_rsp);
void user_resume_context(u64 pml4_phys, const ProcessContext *context);
void user_return_to_kernel_now(void) __attribute__((noreturn));

#endif
