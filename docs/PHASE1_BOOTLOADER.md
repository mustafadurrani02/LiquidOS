# Phase 1: Bootloader

This document explains the first bootloader checkpoint. The project has since
been extended through the kernel and desktop phases, so the final expected boot
result is now the graphical LiquidOS desktop rather than the older protected
mode text checkpoint.

## What Happens When The VM Starts

1. VirtualBox starts a virtual PC.
2. The virtual BIOS looks for bootable media.
3. BIOS finds the El Torito boot record in `liquidos.iso`.
4. BIOS treats the embedded floppy image as the boot disk.
5. BIOS loads the first 512 bytes to memory address `0x7C00`.
6. That first sector is `boot/stage1/stage1.asm`.
7. Stage 1 reads Stage 2 into memory at `0x8000`.
8. Stage 1 jumps to Stage 2.
9. Stage 2 enables A20 and enters 32-bit protected mode.
10. Stage 2 builds page tables, enters x86_64 long mode, and jumps to the kernel.

## Why There Are Two Stages

The BIOS only gives us 512 bytes at first. That is enough for basic setup and disk loading, but not enough for a clean bootloader.

So we split the loader:

- Stage 1: tiny, exactly 512 bytes.
- Stage 2: larger, easier to read, and able to prepare the CPU.

This is how many real boot flows are structured, even though their details differ.

## Why A20 Matters

Old x86 machines wrapped memory addresses at the 1 MiB boundary. Modern 64-bit operating systems need normal addresses above 1 MiB, so the bootloader enables the A20 line before later phases start using high memory.

In Phase 1 we use the fast A20 method through port `0x92`, which VirtualBox supports.

## Why Protected Mode Matters

The CPU starts in 16-bit real mode for BIOS compatibility. x86_64 long mode cannot be reached directly from real mode.

The simplified path is:

```text
16-bit real mode -> 32-bit protected mode -> 64-bit long mode
```

Phase 1 stops at protected mode. Phase 2 will continue into long mode.

## How To Test

Open PowerShell in the project folder:

```powershell
cd "C:\Users\musta\Documents\Codex\2026-05-31\you-are-an-expert-operating-system"
.\scripts\run-virtualbox.ps1
```

The old Phase 1-only checkpoint looked like this:

```text
LiquidOS Phase 1 Bootloader
Success: Stage 2 entered 32-bit protected mode.
Next: Phase 2 will load a 64-bit kernel.
The VM may be closed after this checkpoint.
```

The current full project should instead continue into the graphical desktop.

## How To Debug

If PowerShell says NASM is missing:

- install NASM
- close PowerShell
- open a new PowerShell window
- run `.\scripts\check-tools.ps1`

If VirtualBox does not start:

- install VirtualBox
- close PowerShell
- open a new PowerShell window
- run `.\scripts\check-tools.ps1`

If the VM opens but says there is no bootable medium:

- run `.\scripts\make-iso.ps1`
- verify `build\liquidos.iso` exists
- attach that ISO manually in VirtualBox storage settings

If Stage 1 appears but Stage 2 does not:

- Stage 1 is running
- the most likely problem is disk sector layout
- run `.\scripts\clean.ps1`
- run `.\scripts\make-iso.ps1`
- run `.\scripts\run-virtualbox.ps1`

## Files Built In This Phase

`boot/include/layout.inc`

Shared constants for the boot image. Stage 1 and Stage 2 both use it so they agree on where Stage 2 lives.

`boot/stage1/stage1.asm`

The exact 512-byte boot sector. It prints early messages and loads Stage 2.

`boot/stage2/stage2.asm`

The second-stage loader. It sets up A20, a GDT, and protected mode.

`scripts/build.ps1`

Builds the binary files with NASM and verifies the boot sector signature.

`scripts/make-image.ps1`

Creates the 1.44 MB boot image.

`scripts/make-iso.ps1`

Creates the bootable ISO without requiring any external ISO tool.

`scripts/run-virtualbox.ps1`

Creates or updates a VirtualBox VM and starts it with the ISO attached.
