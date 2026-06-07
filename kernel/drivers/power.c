#include <liquidos/io.h>
#include <liquidos/power.h>

void power_reboot(void) {
    for (u32 i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
        cpu_pause();
    }

    outb(0x64, 0xFE);

    for (;;) {
        cpu_pause();
    }
}

void power_shutdown(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    for (;;) {
        cpu_pause();
    }
}
