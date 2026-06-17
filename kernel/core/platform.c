#include <liquidos/disk.h>
#include <liquidos/fs.h>
#include <liquidos/platform.h>
#include <liquidos/serial.h>

static PlatformCapability capabilities[] = {
    { "hardware", "ATA PIO disk", PLATFORM_MISSING, "IDE-compatible sector I/O probe" },
    { "hardware", "NVMe disk", PLATFORM_PLANNED, "PCIe storage driver not implemented" },
    { "hardware", "USB keyboard/mouse", PLATFORM_PLANNED, "PS/2 input is current path" },
    { "hardware", "PS/2 keyboard/mouse", PLATFORM_AVAILABLE, "polling input path with event queue" },
    { "hardware", "framebuffer display", PLATFORM_PARTIAL, "VBE linear framebuffer, no GPU acceleration" },
    { "hardware", "GPU driver", PLATFORM_PLANNED, "mode setting/acceleration not implemented" },
    { "hardware", "Ethernet/Wi-Fi", PLATFORM_PLANNED, "no NIC driver or wireless stack yet" },
    { "hardware", "audio", PLATFORM_PLANNED, "no PCM mixer or device driver yet" },
    { "hardware", "ACPI/battery", PLATFORM_PLANNED, "no ACPI table parser yet" },
    { "hardware", "touchpad", PLATFORM_PLANNED, "needs USB/HID or PS/2 touchpad support" },

    { "storage", "LiquidFS persistence", PLATFORM_PARTIAL, "RAM filesystem with optional ATA sector save" },
    { "storage", "directories", PLATFORM_PARTIAL, "path-like names only, no directory objects" },
    { "storage", "file permissions", PLATFORM_PLANNED, "no owner/mode bits yet" },
    { "storage", "mount/unmount", PLATFORM_PLANNED, "single built-in filesystem namespace" },
    { "storage", "journal/recovery", PLATFORM_PARTIAL, "checksum detects bad saves, no journal replay" },
    { "storage", "large disks", PLATFORM_PLANNED, "fixed small boot image layout" },
    { "storage", "installer/updater layout", PLATFORM_PLANNED, "store packages exist, OS update layout does not" },

    { "isolation", "ring-3 apps", PLATFORM_AVAILABLE, "iretq entry and int 0x80 return path" },
    { "isolation", "per-process address spaces", PLATFORM_PARTIAL, "user spaces exist, shared kernel map retained" },
    { "isolation", "CR3 switching", PLATFORM_PARTIAL, "switches on user entry/resume, not full preemption" },
    { "isolation", "memory protection", PLATFORM_PARTIAL, "user/kernel page flags and guard pages" },
    { "isolation", "process kill/crash handling", PLATFORM_PLANNED, "page faults still panic the kernel" },
    { "isolation", "resource limits", PLATFORM_PARTIAL, "file-descriptor limits and syscall masks" },
    { "isolation", "sandboxing", PLATFORM_PARTIAL, "package syscall masks only" },

    { "scheduler", "PIT timer ticks", PLATFORM_AVAILABLE, "100 Hz timer accounting" },
    { "scheduler", "cooperative multitasking", PLATFORM_AVAILABLE, "SYS_YIELD between user apps" },
    { "scheduler", "preemptive multitasking", PLATFORM_PLANNED, "timer does not yet interrupt user apps safely" },
    { "scheduler", "priorities", PLATFORM_PLANNED, "round-robin records only" },
    { "scheduler", "sleep/block/wait queues", PLATFORM_PLANNED, "no blocking scheduler queues yet" },
    { "scheduler", "process cleanup", PLATFORM_PARTIAL, "exit state recorded, memory reclamation pending" },
    { "scheduler", "threads", PLATFORM_PLANNED, "processes only" },

    { "app api", "basic syscalls", PLATFORM_AVAILABLE, "write, exit, yield, pid, ticks, files, install" },
    { "app api", "create window", PLATFORM_PLANNED, "desktop windows are kernel-owned" },
    { "app api", "draw text/shapes/images", PLATFORM_PLANNED, "no user drawing ABI yet" },
    { "app api", "input events", PLATFORM_PLANNED, "events go to kernel apps only" },
    { "app api", "timers", PLATFORM_PARTIAL, "ticks syscall exists" },
    { "app api", "clipboard", PLATFORM_PLANNED, "not implemented" },
    { "app api", "notifications", PLATFORM_PLANNED, "not implemented" },
    { "app api", "menus/dialogs", PLATFORM_PLANNED, "not implemented" },

    { "networking", "NIC driver", PLATFORM_PLANNED, "no device driver yet" },
    { "networking", "ARP/IP/ICMP", PLATFORM_PLANNED, "network stack not implemented" },
    { "networking", "UDP/TCP", PLATFORM_PLANNED, "network stack not implemented" },
    { "networking", "DNS/TLS/HTTP", PLATFORM_PLANNED, "browser has offline shell only" },
    { "networking", "updates/downloads", PLATFORM_PLANNED, "offline package catalog only" },

    { "security", "app permissions", PLATFORM_PARTIAL, "syscall masks validated by packages" },
    { "security", "users/accounts", PLATFORM_PLANNED, "single-session OS" },
    { "security", "login/session lock", PLATFORM_PLANNED, "not implemented" },
    { "security", "signed packages", PLATFORM_PLANNED, "LPKG descriptors are unsigned" },
    { "security", "secure boot", PLATFORM_PLANNED, "BIOS boot path only" },
    { "security", "privilege separation", PLATFORM_PARTIAL, "ring 3 apps, kernel services not split" },

    { "services", "init/service manager", PLATFORM_PLANNED, "kernel boot sequence is static" },
    { "services", "settings daemon", PLATFORM_PARTIAL, "theme state persists through LiquidFS" },
    { "services", "app install service", PLATFORM_PARTIAL, "kernel Store installer" },
    { "services", "file indexing/search", PLATFORM_PLANNED, "not implemented" },
    { "services", "notifications", PLATFORM_PLANNED, "not implemented" },
    { "services", "network manager", PLATFORM_PLANNED, "blocked on networking" },
    { "services", "crash reporter/logger", PLATFORM_PARTIAL, "serial logs and panic screen only" },

    { "tooling", "emulator scripts", PLATFORM_AVAILABLE, "macOS/QEMU and Windows helper scripts" },
    { "tooling", "app SDK docs", PLATFORM_PARTIAL, "README documents LAPP/LPKG basics" },
    { "tooling", "compiler target", PLATFORM_PLANNED, "no standalone app toolchain yet" },
    { "tooling", "package builder", PLATFORM_PLANNED, "LPKG generated in kernel catalog" },
    { "tooling", "debugger support", PLATFORM_PLANNED, "serial logs only" },

    { "recovery", "panic/error screen", PLATFORM_PARTIAL, "kernel panic screen and serial diagnostics" },
    { "recovery", "boot menu", PLATFORM_PLANNED, "single boot path" },
    { "recovery", "safe mode", PLATFORM_PLANNED, "not implemented" },
    { "recovery", "installer/updater", PLATFORM_PLANNED, "not implemented" },
    { "recovery", "backups/restore", PLATFORM_PLANNED, "not implemented" },
    { "recovery", "accessibility", PLATFORM_PLANNED, "not implemented" },
};

void platform_init(void) {
    capabilities[0].status = disk_is_available() ? PLATFORM_AVAILABLE : PLATFORM_MISSING;
    capabilities[0].detail = disk_is_available() ? "IDE-compatible sector I/O detected" : "no ATA disk interface detected";
    capabilities[10].detail = fs_persistence_available() ? "LiquidFS saves to ATA sectors" : "LiquidFS is RAM-only in this VM";
    serial_write_line("Platform capability registry initialized");
}

size_t platform_capability_count(void) {
    return sizeof(capabilities) / sizeof(capabilities[0]);
}

const PlatformCapability *platform_capability_get(size_t index) {
    if (index >= platform_capability_count()) {
        return NULL;
    }
    return &capabilities[index];
}

const char *platform_status_name(PlatformStatus status) {
    switch (status) {
    case PLATFORM_AVAILABLE: return "available";
    case PLATFORM_PARTIAL: return "partial";
    case PLATFORM_PLANNED: return "planned";
    case PLATFORM_MISSING: return "missing";
    default: return "unknown";
    }
}

PlatformSummary platform_summary(void) {
    PlatformSummary summary = { 0, 0, 0, 0 };
    for (size_t i = 0; i < platform_capability_count(); i++) {
        switch (capabilities[i].status) {
        case PLATFORM_AVAILABLE: summary.available++; break;
        case PLATFORM_PARTIAL: summary.partial++; break;
        case PLATFORM_PLANNED: summary.planned++; break;
        case PLATFORM_MISSING: summary.missing++; break;
        default: break;
        }
    }
    return summary;
}
