#include <liquidos/boot.h>
#include <liquidos/app.h>
#include <liquidos/app_store.h>
#include <liquidos/debug.h>
#include <liquidos/disk.h>
#include <liquidos/framebuffer.h>
#include <liquidos/gdt.h>
#include <liquidos/fs.h>
#include <liquidos/gfx.h>
#include <liquidos/input.h>
#include <liquidos/interrupts.h>
#include <liquidos/io.h>
#include <liquidos/lib.h>
#include <liquidos/loader.h>
#include <liquidos/pmm.h>
#include <liquidos/platform.h>
#include <liquidos/process.h>
#include <liquidos/ps2.h>
#include <liquidos/scheduler.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>
#include <liquidos/ui.h>
#include <liquidos/vmm.h>

extern u8 kernel_stack_top;
extern u8 user_trap_stack_top;

static void vga_text_fallback(const char *message) {
    volatile u16 *vga = (volatile u16 *)0xB8000;
    for (u32 i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0720;
    }

    u32 pos = 0;
    while (*message && pos < 80 * 25) {
        if (*message == '\n') {
            pos = ((pos / 80) + 1) * 80;
        } else {
            vga[pos++] = (u16)(0x0A00 | (u8)*message);
        }
        message++;
    }
}

void kernel_main(const BootInfo *boot) {
    serial_init();
    serial_write_line("LiquidOS kernel entered long mode");

    if (!boot || boot->magic != BOOT_INFO_MAGIC) {
        vga_text_fallback("LiquidOS kernel received invalid boot info.\n");
        for (;;) {
            cpu_pause();
        }
    }

    pmm_init(boot);
    gdt_init(&user_trap_stack_top);
    vmm_init();
    process_init();
    scheduler_init();
    syscall_init();
    disk_init();
    fs_init();
    platform_init();
    loader_init();
    app_store_init();
    app_init();
    input_queue_init();
    framebuffer_init(boot);
    gfx_init(framebuffer_get());

    if (!gfx_is_available()) {
        vga_text_fallback("LiquidOS could not start graphics.\nCheck VirtualBox video settings.\n");
        for (;;) {
            cpu_pause();
        }
    }

    ps2_init();

    interrupts_init();
    if (syscall_call0(SYS_GETPID) != scheduler_current_pid()) {
        panic("Syscall gate self-test failed");
    }
    serial_write_line("Syscall gate self-test passed");
    pit_init(100);
    interrupts_enable_irq(0);

    ui_init(boot);
    interrupts_enable();
    u64 software_ticks = 0;

    for (;;) {
        InputEvent event;
        while (ps2_poll(&event)) {
            input_queue_push(&event);
        }

        while (input_queue_pop(&event)) {
            if (event.type == INPUT_EVENT_TICK) {
                ui_update(pit_ticks());
            } else {
                ui_handle_event(&event);
                if (event.type == INPUT_EVENT_MOUSE) {
                    ui_render();
                }
            }
        }

        ui_update(software_ticks++);
        ui_render();

        for (u32 i = 0; i < 8; i++) {
            cpu_pause();
        }
    }
}
