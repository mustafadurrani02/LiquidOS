#include <liquidos/disk.h>
#include <liquidos/io.h>
#include <liquidos/lib.h>
#include <liquidos/serial.h>

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

static bool disk_available = false;

static void ata_io_wait(void) {
    outb(0x80, 0);
}

static bool ata_wait_ready(void) {
    for (u32 i = 0; i < 1000000; i++) {
        u8 status = inb(ATA_PRIMARY_IO + 7);
        if ((status & 0x80) == 0 && (status & 0x08)) {
            return true;
        }
        cpu_pause();
    }
    return false;
}

static bool ata_select_lba(u32 lba, u8 command) {
    for (u32 i = 0; i < 100000; i++) {
        if ((inb(ATA_PRIMARY_IO + 7) & 0x80) == 0) {
            break;
        }
        cpu_pause();
    }

    outb(ATA_PRIMARY_CTRL, 0x00);
    outb(ATA_PRIMARY_IO + 6, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_io_wait();
    ata_io_wait();
    ata_io_wait();
    ata_io_wait();
    outb(ATA_PRIMARY_IO + 2, 1);
    outb(ATA_PRIMARY_IO + 3, (u8)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + 4, (u8)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + 5, (u8)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + 7, command);
    return ata_wait_ready();
}

void disk_init(void) {
    outb(ATA_PRIMARY_IO + 6, 0xE0);
    ata_io_wait();
    ata_io_wait();
    ata_io_wait();
    ata_io_wait();
    u8 status = inb(ATA_PRIMARY_IO + 7);
    disk_available = status != 0xFF && status != 0x00;
    serial_write_line(disk_available ? "ATA disk interface detected" : "No ATA disk interface detected");
}

bool disk_is_available(void) {
    return disk_available;
}

bool disk_read_sector(u32 lba, void *buffer) {
    if (!disk_available || !buffer || !ata_select_lba(lba, 0x20)) {
        return false;
    }

    u16 *out = (u16 *)buffer;
    for (u32 i = 0; i < 256; i++) {
        out[i] = inw(ATA_PRIMARY_IO);
    }
    return true;
}

bool disk_write_sector(u32 lba, const void *buffer) {
    if (!disk_available || !buffer || !ata_select_lba(lba, 0x30)) {
        return false;
    }

    const u16 *in = (const u16 *)buffer;
    for (u32 i = 0; i < 256; i++) {
        outw(ATA_PRIMARY_IO, in[i]);
    }
    outb(ATA_PRIMARY_IO + 7, 0xE7);
    return true;
}
