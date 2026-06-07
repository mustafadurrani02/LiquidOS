#include <liquidos/io.h>
#include <liquidos/keyboard.h>
#include <liquidos/mouse.h>
#include <liquidos/ps2.h>

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

static bool wait_input_clear(void) {
    for (u32 i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS) & 0x02) == 0) {
            return true;
        }
        cpu_pause();
    }
    return false;
}

static bool wait_output_full(void) {
    for (u32 i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & 0x01) {
            return true;
        }
        cpu_pause();
    }
    return false;
}

static void write_command(u8 command) {
    if (wait_input_clear()) {
        outb(PS2_COMMAND, command);
    }
}

static void write_data(u8 value) {
    if (wait_input_clear()) {
        outb(PS2_DATA, value);
    }
}

static bool read_data(u8 *value) {
    if (!wait_output_full()) {
        return false;
    }
    *value = inb(PS2_DATA);
    return true;
}

static void flush_output(void) {
    for (u32 i = 0; i < 32; i++) {
        if ((inb(PS2_STATUS) & 0x01) == 0) {
            return;
        }
        (void)inb(PS2_DATA);
    }
}

static void mouse_write(u8 value) {
    u8 ignored;
    write_command(0xD4);
    write_data(value);
    (void)read_data(&ignored);
}

void ps2_init(void) {
    keyboard_init();
    mouse_init_packet_state();

    write_command(0xAD);
    write_command(0xA7);
    flush_output();

    write_command(0x20);
    u8 config = 0;
    (void)read_data(&config);
    config |= 0x03;
    config &= (u8)~0x20;
    write_command(0x60);
    write_data(config);

    write_command(0xAE);
    write_command(0xA8);

    u8 ignored;
    write_data(0xF4);
    (void)read_data(&ignored);

    mouse_write(0xF6);
    mouse_write(0xF3);
    mouse_write(200);
    mouse_write(0xE8);
    mouse_write(0x03);
    mouse_write(0xF4);
    flush_output();
}

bool ps2_poll(InputEvent *event) {
    event->type = INPUT_EVENT_NONE;

    for (u32 i = 0; i < 32; i++) {
        u8 status = inb(PS2_STATUS);
        if ((status & 0x01) == 0) {
            return false;
        }

        u8 value = inb(PS2_DATA);
        if (status & 0x20) {
            if (mouse_handle_byte(value, event)) {
                return true;
            }
        } else {
            if (keyboard_handle_scancode(value, event)) {
                return true;
            }
        }
    }

    return false;
}
