; LiquidOS Stage 1 Boot Sector
; ----------------------------
; This is the first code the BIOS runs from our boot image.
;
; Responsibilities in Phase 1:
;   1. Set up a tiny real-mode environment.
;   2. Print a visible boot message using BIOS video services.
;   3. Load the larger Stage 2 loader from the floppy image.
;   4. Jump to Stage 2 at physical address 0x8000.
;
; This file must assemble to exactly 512 bytes and end with the BIOS
; boot signature 0xAA55. The build script checks both conditions.

BITS 16
ORG 0x7C00

%include "layout.inc"

start:
    jmp short boot_start
    nop

; A minimal FAT12-style BIOS Parameter Block.
; The loader does not parse FAT yet, but this makes the floppy image look
; familiar to BIOS implementations and disk tools.
oem_name:               db "LIQUIDOS"
bytes_per_sector:       dw 512
sectors_per_cluster:    db 1
reserved_sectors:       dw 1
fat_count:              db 2
root_entry_count:       dw 224
total_sectors_16:       dw 2880
media_descriptor:       db 0xF0
sectors_per_fat:        dw 9
sectors_per_track:      dw 18
head_count:             dw 2
hidden_sectors:         dd 0
total_sectors_32:       dd 0
drive_number:           db 0
reserved_1:             db 0
extended_signature:     db 0x29
volume_id:              dd 0x20260531
volume_label:           db "LIQUIDOS   "
filesystem_type:        db "FAT12   "

boot_start:
    cli

    ; BIOS enters at 0000:7C00 on VirtualBox. We make all segment registers
    ; explicit so labels can be addressed predictably from DS=0.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    sti

    mov [boot_drive], dl

    mov si, msg_stage1
    call print_string

    ; Reset the boot disk before reading. This is a small reliability step
    ; that leaves the BIOS disk system in a known state.
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    mov si, msg_loading_stage2
    call print_string

    ; Read Stage 2 from cylinder 0, head 0, sector 2.
    ; The build script guarantees Stage 2 occupies exactly 16 sectors.
    mov ax, STAGE2_LOAD_SEGMENT
    mov es, ax
    mov bx, STAGE2_LOAD_OFFSET
    mov ah, 0x02
    mov al, STAGE2_SECTOR_COUNT
    mov ch, 0x00
    mov cl, STAGE2_START_SECTOR
    mov dh, 0x00
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    mov si, msg_stage2_ready
    call print_string

    mov dl, [boot_drive]
    jmp STAGE2_LOAD_SEGMENT:STAGE2_LOAD_OFFSET

disk_error:
    mov si, msg_disk_error
    call print_string

.halt:
    cli
    hlt
    jmp .halt

; Prints a zero-terminated string from DS:SI using BIOS teletype output.
; Supports LF as a newline by emitting CR+LF.
print_string:
    lodsb
    test al, al
    jz .done

    cmp al, 0x0A
    je .newline

    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_string

.newline:
    mov ah, 0x0E
    mov al, 0x0D
    int 0x10
    mov al, 0x0A
    int 0x10
    jmp print_string

.done:
    ret

boot_drive:             db 0

msg_stage1:             db "LiquidOS Stage 1: BIOS boot sector running.", 0x0A, 0
msg_loading_stage2:     db "Loading Stage 2 from boot image...", 0x0A, 0
msg_stage2_ready:       db "Stage 2 loaded. Jumping now.", 0x0A, 0
msg_disk_error:         db "Disk read failed. Check the boot image.", 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55
