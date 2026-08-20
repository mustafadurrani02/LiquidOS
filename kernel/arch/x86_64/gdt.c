#include <liquidos/gdt.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>

typedef struct GdtPointer {
    u16 limit;
    u64 base;
} __attribute__((packed)) GdtPointer;

typedef struct Tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed)) Tss;

extern void gdt_load(const GdtPointer *pointer);

static u64 gdt[7];
static Tss tss;

static u64 descriptor(u32 base, u32 limit, u8 access, u8 flags) {
    return ((u64)(limit & 0xFFFF)) |
           ((u64)(base & 0xFFFFFF) << 16) |
           ((u64)access << 40) |
           ((u64)((limit >> 16) & 0x0F) << 48) |
           ((u64)(flags & 0x0F) << 52) |
           ((u64)((base >> 24) & 0xFF) << 56);
}

static void set_tss_descriptor(u16 index, u64 base, u32 limit) {
    gdt[index] = descriptor((u32)base, limit, 0x89, 0x0);
    gdt[index + 1] = base >> 32;
}

void gdt_init(void *kernel_stack_top) {
    memset(gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    tss.rsp0 = (u64)(uintptr_t)kernel_stack_top;
    tss.iomap_base = sizeof(Tss);

    gdt[1] = descriptor(0, 0, 0x9A, 0xA);
    gdt[2] = descriptor(0, 0, 0x92, 0xC);
    gdt[3] = descriptor(0, 0, 0xF2, 0xC);
    gdt[4] = descriptor(0, 0, 0xFA, 0xA);
    set_tss_descriptor(5, (u64)(uintptr_t)&tss, sizeof(Tss) - 1);

    GdtPointer pointer;
    pointer.limit = sizeof(gdt) - 1;
    pointer.base = (u64)(uintptr_t)gdt;
    gdt_load(&pointer);
    serial_write_line("GDT/TSS initialized");
}

u64 tss_kernel_rsp0(void) {
    return tss.rsp0;
}
