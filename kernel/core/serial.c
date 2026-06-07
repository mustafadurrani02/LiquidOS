#include <liquidos/io.h>
#include <liquidos/serial.h>

#define COM1 0x3F8

static bool serial_ready = false;

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    serial_ready = true;
}

void serial_write_char(char ch) {
    if (!serial_ready) {
        return;
    }

    for (u32 i = 0; i < 100000; i++) {
        if (inb(COM1 + 5) & 0x20) {
            outb(COM1, (u8)ch);
            return;
        }
    }
}

void serial_write(const char *text) {
    while (*text) {
        if (*text == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*text++);
    }
}

void serial_write_line(const char *text) {
    serial_write(text);
    serial_write("\n");
}

void serial_write_hex(u64 value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_write_char(digits[(value >> shift) & 0xF]);
    }
}
