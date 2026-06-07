# Implemented Phases

The project has been carried through the requested phases as a single runnable teaching OS.

## Phase 1: Bootloader

Files:

- `boot/stage1/stage1.asm`
- `boot/stage2/stage2.asm`
- `boot/include/layout.inc`

What it does:

- BIOS loads Stage 1 at `0x7C00`.
- Stage 1 loads Stage 2 from fixed sectors.
- Stage 2 reads the BIOS memory map.
- Stage 2 selects a VBE framebuffer graphics mode.
- Stage 2 loads the kernel from fixed sectors.
- Stage 2 enters protected mode, builds page tables, enters x86_64 long mode, and jumps to the kernel.

## Phase 2: Kernel

Files:

- `kernel/arch/x86_64/entry.asm`
- `kernel/core/kernel.c`
- `kernel/linker.ld`

What it does:

- Starts at physical address `1 MiB`.
- Clears `.bss`.
- Creates a kernel stack.
- Calls `kernel_main`.
- Uses no host operating system or C standard library.

## Phase 3: Memory Management

Files:

- `kernel/mm/pmm.c`
- `kernel/include/liquidos/pmm.h`

What it does:

- Parses the BIOS memory map.
- Tracks total and usable memory.
- Provides a simple aligned kernel heap with `kmalloc`.

This is a foundation, not a full page-frame allocator yet.

## Phase 4: Graphics System

Files:

- `kernel/drivers/framebuffer.c`
- `kernel/gfx/gfx.c`
- `kernel/gfx/font.c`

What it does:

- Uses the VBE linear framebuffer selected by the bootloader.
- Draws into a RAM back buffer.
- Copies the back buffer to the real framebuffer each frame.
- Provides rectangles, text, and cursor drawing.

## Phase 5: Desktop Environment

Files:

- `kernel/ui/desktop.c`

What it does:

- Draws the desktop background.
- Draws a taskbar.
- Opens built-in terminal and file explorer windows.

## Phase 6: Mouse Support

Files:

- `kernel/drivers/ps2.c`
- `kernel/drivers/mouse.c`

What it does:

- Initializes the PS/2 controller.
- Enables the PS/2 mouse.
- Decodes 3-byte mouse packets.
- Moves the cursor.
- Supports left-click and window dragging.

## Phase 7: Keyboard Support

Files:

- `kernel/drivers/keyboard.c`
- `kernel/drivers/ps2.c`

What it does:

- Reads keyboard scancodes.
- Handles Shift.
- Converts common keys to ASCII.
- Sends typed characters to the terminal.

## Phase 8: Window Manager

Files:

- `kernel/ui/desktop.c`

What it does:

- Tracks focused window.
- Tracks z-order.
- Supports titlebar dragging.
- Supports close buttons.

## Phase 9: Terminal

Files:

- `kernel/ui/terminal.c`

Commands:

- `help`
- `clear`
- `about`
- `mem`
- `ls`
- `reboot`
- `shutdown`

## Phase 10: Filesystem Foundation

Files:

- `kernel/fs/initramfs.c`
- `kernel/include/liquidos/fs.h`

What it does:

- Provides a tiny read-only in-kernel file table.
- Lets the file explorer list files.
- Lets the terminal `ls` command list files.

This is intentionally simple. A real filesystem parser can be added later without changing the desktop API.
