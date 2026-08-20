#include <liquidos/input.h>
#include <liquidos/gdt.h>
#include <liquidos/interrupts.h>
#include <liquidos/io.h>
#include <liquidos/keyboard.h>
#include <liquidos/lib.h>
#include <liquidos/mouse.h>
#include <liquidos/process.h>
#include <liquidos/scheduler.h>
#include <liquidos/syscall.h>
#include <liquidos/usermode.h>
#include <liquidos/vmm.h>
#include <liquidos/debug.h>
#include <liquidos/serial.h>

#define IDT_ENTRIES 256
#define KERNEL_CODE_SELECTOR GDT_KERNEL_CODE

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20
#define IRQ_BASE 32

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_BASE_HZ 1193182U

#define PS2_DATA 0x60
#define PS2_STATUS 0x64

typedef struct IdtEntry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 attributes;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed)) IdtEntry;

typedef struct IdtPointer {
    u16 limit;
    u64 base;
} __attribute__((packed)) IdtPointer;

extern void *isr_stub_table[];
extern void isr_stub_128(void);

static IdtEntry idt[IDT_ENTRIES];
static volatile u64 ticks = 0;
static u16 pic_mask = 0xFFFF;

static u64 read_cr2(void) {
    u64 value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static bool frame_from_user(const InterruptFrame *frame) {
    return frame && ((frame->cs & 3) == 3);
}

static void stop_crashed_user(InterruptFrame *frame, u64 fault_address) {
    serial_write("User process crashed vector ");
    char number[24];
    u64_to_dec(frame->vector, number, sizeof(number));
    serial_write(number);
    serial_write(" error ");
    u64_to_hex(frame->error_code, number, sizeof(number));
    serial_write(number);
    serial_write(" rip ");
    serial_write_hex(frame->rip);
    if (frame->vector == 14) {
        serial_write(" cr2 ");
        serial_write_hex(fault_address);
    }
    serial_write_line("");

    process_crash_current(frame->vector, frame->error_code, frame->rip, fault_address);
    vmm_switch(vmm_kernel_space());
    user_return_to_kernel_now();
}

static void idt_set_gate(u8 vector, u64 handler) {
    idt[vector].offset_low = (u16)(handler & 0xFFFF);
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].ist = 0;
    idt[vector].attributes = 0x8E;
    idt[vector].offset_mid = (u16)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (u32)((handler >> 32) & 0xFFFFFFFF);
    idt[vector].zero = 0;
}

static void idt_set_trap_gate(u8 vector, u64 handler, u8 dpl) {
    idt[vector].offset_low = (u16)(handler & 0xFFFF);
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].ist = 0;
    idt[vector].attributes = (u8)(0x8F | ((dpl & 3) << 5));
    idt[vector].offset_mid = (u16)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (u32)((handler >> 32) & 0xFFFFFFFF);
    idt[vector].zero = 0;
}

static void lidt(const IdtPointer *pointer) {
    __asm__ volatile("lidt (%0)" : : "r"(pointer));
}

static void io_wait(void) {
    outb(0x80, 0);
}

static void pic_write_masks(void) {
    outb(PIC1_DATA, (u8)(pic_mask & 0xFF));
    outb(PIC2_DATA, (u8)((pic_mask >> 8) & 0xFF));
}

static void pic_remap(void) {
    u8 mask1 = inb(PIC1_DATA);
    u8 mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, IRQ_BASE);
    io_wait();
    outb(PIC2_DATA, IRQ_BASE + 8);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    pic_mask = (u16)mask1 | ((u16)mask2 << 8);
    pic_write_masks();
}

static void pic_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void interrupts_enable_irq(u8 irq) {
    if (irq >= 16) {
        return;
    }
    pic_mask &= (u16)~(1U << irq);
    if (irq >= 8) {
        pic_mask &= (u16)~(1U << 2);
    }
    pic_write_masks();
}

void interrupts_disable_irq(u8 irq) {
    if (irq >= 16) {
        return;
    }
    pic_mask |= (u16)(1U << irq);
    pic_write_masks();
}

u64 pit_ticks(void) {
    return ticks;
}

void pit_init(u32 frequency_hz) {
    if (frequency_hz == 0) {
        frequency_hz = 100;
    }
    u32 divisor = PIT_BASE_HZ / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 65535) {
        divisor = 65535;
    }

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (u8)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (u8)((divisor >> 8) & 0xFF));
}

void interrupts_init(void) {
    interrupts_disable();

    for (u8 i = 0; i < 48; i++) {
        idt_set_gate(i, (u64)(uintptr_t)isr_stub_table[i]);
    }
    idt_set_trap_gate(0x80, (u64)(uintptr_t)isr_stub_128, 3);

    IdtPointer pointer;
    pointer.limit = (u16)(sizeof(idt) - 1);
    pointer.base = (u64)(uintptr_t)idt;
    lidt(&pointer);

    pic_remap();
    pic_mask = 0xFFFF;
    pic_write_masks();

    serial_write_line("Interrupts initialized");
}

void interrupt_dispatch(InterruptFrame *frame) {
    u64 vector = frame->vector;
    process_save_interrupt_frame(frame);

    if (vector == 0x80) {
        frame->rax = syscall_dispatch(frame);
        return;
    }

    if (vector == IRQ_BASE) {
        ticks++;
        InputEvent event;
        event.type = INPUT_EVENT_TICK;
        event.ch = 0;
        event.dx = 0;
        event.dy = 0;
        event.left_down = false;
        event.right_down = false;
        event.middle_down = false;
        input_queue_push(&event);
        pic_eoi(0);
        if (frame_from_user(frame) && process_preempt_current()) {
            vmm_switch(vmm_kernel_space());
            user_return_to_kernel_now();
        }
        scheduler_tick();
        return;
    }

    if (vector == IRQ_BASE + 1) {
        u8 value = inb(PS2_DATA);
        InputEvent event;
        if (keyboard_handle_scancode(value, &event)) {
            input_queue_push(&event);
        }
        pic_eoi(1);
        return;
    }

    if (vector == IRQ_BASE + 12) {
        u8 status = inb(PS2_STATUS);
        u8 value = inb(PS2_DATA);
        (void)status;
        InputEvent event;
        if (mouse_handle_byte(value, &event)) {
            input_queue_push(&event);
        }
        pic_eoi(12);
        return;
    }

    if (vector >= IRQ_BASE && vector < IRQ_BASE + 16) {
        pic_eoi((u8)(vector - IRQ_BASE));
        return;
    }

    if (vector == 14) {
        u64 fault_address = read_cr2();
        vmm_report_page_fault(fault_address, frame->error_code, frame->rip);
        if (frame_from_user(frame)) {
            stop_crashed_user(frame, fault_address);
        }
        panic("Page fault");
    }

    if (vector < 32 && frame_from_user(frame)) {
        stop_crashed_user(frame, 0);
    }

    serial_write("CPU exception vector ");
    char number[24];
    u64_to_dec(vector, number, sizeof(number));
    serial_write(number);
    serial_write(" error ");
    u64_to_hex(frame->error_code, number, sizeof(number));
    serial_write(number);
    serial_write(" rip ");
    serial_write_hex(frame->rip);
    serial_write_line("");
    panic("CPU exception");
}
