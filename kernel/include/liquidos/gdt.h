#ifndef LIQUIDOS_GDT_H
#define LIQUIDOS_GDT_H

#include <liquidos/types.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x1B
#define GDT_USER_CODE   0x23
#define GDT_TSS         0x28

void gdt_init(void *kernel_stack_top);
u64 tss_kernel_rsp0(void);

#endif
