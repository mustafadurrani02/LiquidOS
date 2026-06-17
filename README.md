# LiquidOS

LiquidOS is a completely original beginner-oriented x86_64 operating system.

It uses:

- a custom BIOS boot sector
- a custom second-stage bootloader
- a freestanding C kernel
- x86_64 long mode
- VBE framebuffer graphics
- double-buffered drawing
- PS/2 keyboard input
- PS/2 mouse input
- a macOS-inspired desktop shell with a top floating dock/menu bar, desktop shortcuts, Control Center, windows, terminal, file explorer placeholder, shutdown button, and reboot button
- a graphical Store install/open/remove flow, app launcher, and System Settings window with desktop themes
- a native Liqueia browser shell with tabs, address input, bookmarks, history, and local pages
- an interrupt-driven PIT timer, CPU exception reporting, syscall gate, process table, cooperative user scheduling, page-frame allocator, and VMM groundwork
- an offline app catalog that validates simple `LPKG1` packages, installs runnable demo apps, receipts, and manifests into the LiquidOS filesystem
- a platform capability registry that tracks hardware, storage, isolation, scheduling, app API, networking, security, services, tooling, and recovery readiness
- a larger 1120-sector kernel reserve in the fixed boot image

No Linux, BSD, ReactOS, TempleOS, or existing operating-system code is used.

## Kernel Platform Work

LiquidOS now has the first pieces of a real OS platform:

- x86_64 IDT/PIC interrupt handling with a 100 Hz PIT timer
- CPU exception diagnostics that report vector, error code, and RIP to serial, with user-mode crashes contained to the offending process
- a GDT with kernel/user segments plus a TSS and dedicated ring-3 trap stack
- scheduler ticks and a process table with kernel tasks plus saved user trap contexts for cooperative `SYS_YIELD` and first timer-driven ring-3 preemption
- an `int 0x80` syscall gate with basic syscalls for write, exit, yield, PID, uptime ticks, filesystem open/read/write/close, spawning a user stub, installing catalog apps, and early window handles
- a 4 KiB page-frame allocator layered on the BIOS memory map
- VMM helpers for page mapping, unmapping, user/kernel permissions, guard pages, per-process address-space records, and page-fault diagnostics
- a flat binary `LAPP` format with a validated header, text/data/bss/stack metadata, syscall mask, and built-in `HELLO`, `APP_A`, and `APP_B` demo apps
- a `LAPP` loader that maps app text/data/bss into user memory, enters ring 3 with `iretq`, handles `SYS_WRITE`, `SYS_YIELD`, and `SYS_EXIT`, and returns safely to the kernel
- per-process file descriptor tables for `open`, `read`, `write`, and `close`
- terminal commands for `ps`, `spawn`, `runhello`, `runapps`, `runcrash`, `syscall`, `apps`, `download NAME`, `runapp NAME`, and `uninstall NAME`
- a graphical Store window that validates offline catalog packages, installs runnable `LAPP` payloads into LiquidFS, runs installed apps, and removes installed packages
- a Launch Apps window that opens built-in apps and runs installed Store apps
- an App Manager inside System Settings for running/removing installed apps and viewing filesystem persistence status
- terminal platform status commands for `platform`, `drivers`, `services`, and `security`
- desktop customization through System Settings with Liquid Gold, Aurora Blue, Glass Mint, and Night Violet themes persisted in `SYSTEM/THEME.TXT`

Keyboard and mouse input still use the stable PS/2 polling path while timer IRQs
drive scheduler accounting. User processes now carry entry RIP, user RSP,
address-space, guard-page, exit-code, syscall-mask, file descriptor, and saved
trap-frame metadata. User exceptions now stop the offending app and preserve
crash vector/error/RIP/fault-address details for `ps`. Timer IRQs can preempt
ring-3 apps back into the kernel loader loop, but full desktop-wide preemptive
scheduling, memory reclamation, and ELF loading are intentionally not
implemented yet; the current app model still uses the simpler flat `LAPP`
binary format.

### User App ABI

User apps can use the existing filesystem/process syscalls plus an early window
ABI:

- `SYS_WINDOW_CREATE`
- `SYS_DRAW_TEXT`
- `SYS_DRAW_RECT`
- `SYS_POLL_EVENT`
- `SYS_WINDOW_CLOSE`

The window ABI currently allocates and validates kernel-owned window handles.
Actual desktop rendering and event routing for user-created windows are the next
step.

### Platform Capability Registry

LiquidOS keeps an in-kernel registry for the big operating-system areas that
need to exist before it can become a serious desktop platform. Each item is
reported as `available`, `partial`, `planned`, or `missing`, so the OS can show
what is real today without pretending huge subsystems like NVMe, Wi-Fi, TLS, or
audio are finished.

The registry covers hardware drivers, storage, process isolation, scheduling,
app/window APIs, networking, security, system services, developer tooling, and
recovery/polish. Use `platform` in Terminal for the full list, or `drivers`,
`services`, and `security` for filtered views. System Settings also shows a
compact platform readiness summary next to App Manager.

### Flat LAPP Format

`LAPP` files are binary app packages stored in LiquidFS. Version 1 contains:

- magic/version/header size
- entry offset inside text
- text offset and size
- data offset and size
- bss size
- requested stack size
- syscall mask

The loader validates the header, maps text executable/user, maps data and bss
user-writable, creates a guarded user stack, and starts the app in ring 3.
`runhello` runs `APPS/HELLO.APP`; `runapps` loads `APPS/APP_A.APP` and
`APPS/APP_B.APP`; `runcrash` runs `APPS/CRASH.APP`, which intentionally page
faults from ring 3 to test crash-safe process termination.

## Store and Personalization

The LiquidOS Store is currently an offline package catalog. Catalog packages use
a simple text `LPKG1` descriptor stored in `STORE/PACKAGES/`. The installer
validates package magic/version, app metadata, entry path, payload path,
requested syscall permissions, payload `LAPP` magic, and available LiquidFS file
slots before copying the runnable payload into `APPS/`.

Successful installs create:

- the runnable app payload in `APPS/`
- a receipt in `STORE/INSTALLED/`
- an app manifest in `STORE/MANIFEST/`

Installed apps can run directly from the Store, Launch Apps, App Manager, or the
terminal with `runapp NAME`. They can be removed from the Store, App Manager, or
terminal with `uninstall NAME`.

Launch Apps opens from the taskbar search pill. Networking, remote package
download, signatures, dependency resolution, and third-party binary tooling are
intentionally deferred.

The top floating dock/menu bar keeps the LiquidOS menu on the left, centered app
icons, launcher search, and right-side control widgets for Wi-Fi, battery status,
clock, and Control Center. The desktop also has shortcut icons for Launch Apps,
Files, and Store.

System Settings changes the desktop theme immediately. Themes tint the wallpaper
wash, active window glow, Store cards, and dock accents without rebuilding the
OS. The selected theme is saved to `SYSTEM/THEME.TXT` and loaded again when the
desktop starts. System Settings also includes an App Manager and shows whether
LiquidFS is disk-backed or RAM-only in the current VM.

## Liqueia Browser

LiquidOS includes a native port of the original
[Liqueia](https://github.com/mustafadurrani02/Liqueia) browser experience. The
kernel app carries over Liqueia's tab model, address/search workflow, bookmarks,
history, settings, and gold liquid-glass identity without embedding Electron.

External pages currently show a network-unavailable screen because LiquidOS does
not yet have NIC, DNS, TCP/IP, TLS, or an HTML rendering engine. The native shell
is structured so those services can be connected as the OS gains them.

## macOS (Apple Silicon)

LiquidOS is an x86_64 BIOS operating system. VirtualBox on an Apple Silicon
Mac runs Arm guests and cannot run this x86_64 BIOS image. Use QEMU, which can
emulate an x86_64 PC:

```bash
git clone https://github.com/mustafadurrani02/LiquidOS.git
cd LiquidOS
brew install llvm lld nasm qemu
chmod +x scripts/build-macos.sh scripts/run-qemu.sh
./scripts/run-qemu.sh
```

To build without starting the emulator:

```bash
./scripts/build-macos.sh
```

The boot image is written to `build/liquidos.img` and serial output is written
to `build/serial.log`.

## Install These Windows Tools

Install all three tools, then close and reopen PowerShell.

| Tool | Why | Download |
| --- | --- | --- |
| NASM 3.01 | Builds assembly boot code | https://www.nasm.us/pub/nasm/releasebuilds/3.01/win64/nasm-3.01-installer-x64.exe |
| LLVM 22.1.6 | Builds and links the C kernel | https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.6/LLVM-22.1.6-win64.exe |
| VirtualBox 7.2.8 | Runs the OS | https://download.virtualbox.org/virtualbox/7.2.8/VirtualBox-7.2.8-173730-Win.exe |

During LLVM installation, enable the option that adds LLVM to the system PATH if the installer offers it. The scripts also check the normal `C:\Program Files\LLVM\bin` location.

## Open PowerShell

1. Open the Windows Start menu.
2. Type `PowerShell`.
3. Open **Windows PowerShell**.
4. Run:

```powershell
cd "C:\Users\musta\Documents\Codex\2026-05-31\you-are-an-expert-operating-system"
```

## Allow Local Scripts

Run this once:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

Expected result: PowerShell asks for confirmation. Type `Y` and press Enter.

If you do not want to change the policy permanently, run scripts with a temporary bypass:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-tools.ps1
```

## Verify Tools

From the project folder:

```powershell
.\scripts\check-tools.ps1
```

Expected result:

```text
Success: all LiquidOS tools are available.
```

If a tool is missing, install it, close PowerShell, open a new PowerShell window, and run the check again.

If you already installed a tool but the checker still says it is missing, run:

```powershell
.\scripts\diagnose-tools.ps1
```

If the diagnostic script shows the file exists, but the checker still cannot use it, set the paths manually in that same PowerShell window:

```powershell
$env:LIQUIDOS_NASM="C:\Program Files\NASM\nasm.exe"
$env:LIQUIDOS_VBOXMANAGE="C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
$env:LIQUIDOS_LLVM_BIN="C:\Program Files\LLVM\bin"
.\scripts\check-tools.ps1
```

## Build The OS

From the project folder:

```powershell
.\scripts\build.ps1
```

Expected result:

```text
Build complete.
Stage 1: ...\build\stage1.bin (512 bytes)
Stage 2: ...\build\stage2.bin (... bytes, padded to 8192 bytes)
Kernel:  ...\build\kernel.bin (... bytes, ... sectors)
```

Create the boot image and ISO:

```powershell
.\scripts\make-iso.ps1
```

Expected result:

```text
Boot image created.
...\build\liquidos.img

ISO created.
...\build\liquidos.iso
```

## Run In VirtualBox

The easiest path is:

```powershell
.\scripts\run-virtualbox.ps1
```

Expected result:

1. A VM named `LiquidOS` is created or updated.
2. `build\liquidos.iso` is attached as the optical disk.
3. VirtualBox opens.
4. The OS boots into a graphical desktop.

You should see:

- desktop background
- taskbar
- terminal window
- file explorer window
- movable mouse cursor
- Terminal, Files, Reboot, and Shutdown taskbar buttons

Try typing in the terminal:

```text
help
ls
mem
about
clear
```

## Manual VirtualBox Settings

Use these if you create the VM by hand:

- Name: `LiquidOS`
- Type: `Other`
- Version: `Other/Unknown (64-bit)`
- EFI: disabled
- RAM: `256 MB`
- CPUs: `1`
- Boot order: optical first
- Hard disk: none required
- Graphics controller: `VBoxVGA`
- Video memory: `64 MB`
- 3D acceleration: disabled
- Pointing device: `PS/2 Mouse`
- USB: disabled
- Network: disabled
- Audio: disabled
- Optical drive: attach `C:\Users\musta\Documents\Codex\2026-05-31\you-are-an-expert-operating-system\build\liquidos.iso`

## Clean Build Files

```powershell
.\scripts\clean.ps1
```

## Project Layout

```text
boot/
  include/               Boot layout constants
  stage1/                512-byte BIOS boot sector
  stage2/                Long-mode loader

kernel/
  arch/x86_64/           Kernel assembly entry
  core/                  Kernel main and serial logging
  drivers/               Framebuffer, PS/2 keyboard, PS/2 mouse, power
  fs/                    Tiny read-only filesystem foundation
  gfx/                   Drawing, text, double buffering
  include/liquidos/        Kernel headers
  lib/                   Freestanding string/format helpers
  mm/                    Memory map and heap foundation
  ui/                    Desktop, windows, terminal

scripts/
  check-tools.ps1        Verifies required tools
  build.ps1              Builds bootloader and kernel
  make-image.ps1         Creates raw boot image
  make-iso.ps1           Creates bootable ISO
  run-virtualbox.ps1     Creates/updates/runs VirtualBox VM
  clean.ps1              Removes build outputs
```

## Debugging

If the VM does not boot:

1. Run `.\scripts\check-tools.ps1`.
2. Run `.\scripts\clean.ps1`.
3. Run `.\scripts\run-virtualbox.ps1`.
4. Check `build\serial.log` if it exists.

The boot path is intentionally fixed-sector and small so early failures are easier to diagnose.
