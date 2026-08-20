; LiquidOS Stage 2 Bootloader
; ---------------------------
; Stage 1 loads this file to physical address 0x8000 and jumps here.
;
; Responsibilities:
;   1. Gather BIOS memory-map information.
;   2. Select a VBE linear-framebuffer graphics mode.
;   3. Load the flat kernel binary from fixed sectors.
;   4. Enter 32-bit protected mode.
;   5. Copy the kernel to 1 MiB.
;   6. Build simple identity-mapped x86_64 page tables.
;   7. Enter 64-bit long mode and jump to the C kernel.

BITS 16
ORG 0x8000

%include "layout.inc"
%include "kernel_layout.inc"

CODE32_SEG equ 0x08
DATA_SEG   equ 0x10
CODE64_SEG equ 0x18

BOOT_MAGIC  equ 0x4451494C
BOOT_VERSION equ 1
MEMORY_MAP_OFFSET equ 56
MEMORY_MAP_ENTRY_SIZE equ 24
MEMORY_MAP_MAX_ENTRIES equ 32

stage2_start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    sti

    mov [boot_drive], dl

    mov ax, 0x0003
    int 0x10

    mov si, msg_stage2
    call print_string_16

    call init_boot_info
    call read_memory_map
    call set_graphics_mode
    call enable_a20_fast
    call load_kernel

    mov si, msg_entering_pm
    call print_string_16

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp CODE32_SEG:protected_mode_entry

init_boot_info:
    mov edi, BOOT_INFO_ADDRESS
    mov ecx, 512
    xor eax, eax
    rep stosd

    mov dword [BOOT_INFO_ADDRESS + 0], BOOT_MAGIC
    mov dword [BOOT_INFO_ADDRESS + 4], BOOT_VERSION
    xor eax, eax
    mov al, [boot_drive]
    mov dword [BOOT_INFO_ADDRESS + 8], eax
    mov dword [BOOT_INFO_ADDRESS + 12], 0
    mov dword [BOOT_INFO_ADDRESS + 16], 0
    mov dword [BOOT_INFO_ADDRESS + 20], 0
    mov dword [BOOT_INFO_ADDRESS + 24], 0
    mov dword [BOOT_INFO_ADDRESS + 28], 0
    mov dword [BOOT_INFO_ADDRESS + 32], 0
    mov dword [BOOT_INFO_ADDRESS + 36], 0
    mov dword [BOOT_INFO_ADDRESS + 40], KERNEL_LOAD_ADDRESS
    mov dword [BOOT_INFO_ADDRESS + 44], 0
    mov dword [BOOT_INFO_ADDRESS + 48], KERNEL_SIZE_BYTES
    mov dword [BOOT_INFO_ADDRESS + 52], 0
    ret

read_memory_map:
    mov si, msg_memmap
    call print_string_16

    xor ebx, ebx
    xor bp, bp
    xor ax, ax
    mov es, ax
    mov di, BOOT_INFO_ADDRESS + MEMORY_MAP_OFFSET

.next_entry:
    cmp bp, MEMORY_MAP_MAX_ENTRIES
    jae .done

    mov eax, 0x0000E820
    mov edx, 0x534D4150
    mov ecx, MEMORY_MAP_ENTRY_SIZE
    int 0x15
    jc .done

    cmp eax, 0x534D4150
    jne .done

    cmp ecx, 20
    jb .done

    cmp ecx, MEMORY_MAP_ENTRY_SIZE
    jae .has_extended
    mov dword [es:di + 20], 0

.has_extended:
    add di, MEMORY_MAP_ENTRY_SIZE
    inc bp

    test ebx, ebx
    jne .next_entry

.done:
    movzx eax, bp
    mov dword [BOOT_INFO_ADDRESS + 12], eax
    ret

set_graphics_mode:
    mov si, msg_video
    call print_string_16

    mov si, vbe_mode_candidates

.try_mode:
    lodsw
    test ax, ax
    jz .failed

    mov [selected_mode], ax

    xor bx, bx
    mov es, bx
    mov di, VBE_MODE_INFO_ADDR
    mov cx, ax
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .try_mode

    mov al, [VBE_MODE_INFO_ADDR + 25]
    cmp al, 24
    jb .try_mode

    mov ax, [selected_mode]
    or ax, 0x4000
    mov bx, ax
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .try_mode

    mov eax, [VBE_MODE_INFO_ADDR + 40]
    mov dword [BOOT_INFO_ADDRESS + 16], eax
    mov dword [BOOT_INFO_ADDRESS + 20], 0

    xor eax, eax
    mov ax, [VBE_MODE_INFO_ADDR + 18]
    mov dword [BOOT_INFO_ADDRESS + 24], eax

    xor eax, eax
    mov ax, [VBE_MODE_INFO_ADDR + 20]
    mov dword [BOOT_INFO_ADDRESS + 28], eax

    xor eax, eax
    mov ax, [VBE_MODE_INFO_ADDR + 50]
    test ax, ax
    jnz .pitch_ready
    mov ax, [VBE_MODE_INFO_ADDR + 16]

.pitch_ready:
    mov dword [BOOT_INFO_ADDRESS + 32], eax

    xor eax, eax
    mov al, [VBE_MODE_INFO_ADDR + 25]
    mov dword [BOOT_INFO_ADDRESS + 36], eax
    ret

.failed:
    mov ax, 0x0003
    int 0x10
    mov si, msg_video_failed
    call print_string_16
    ret

enable_a20_fast:
    in al, 0x92
    or al, 00000010b
    and al, 11111110b
    out 0x92, al
    ret

load_kernel:
    mov si, msg_kernel
    call print_string_16

    mov cx, KERNEL_SECTOR_COUNT
    mov word [current_sector], KERNEL_START_SECTOR
    mov word [current_segment], KERNEL_TEMP_SEGMENT

.sector_loop:
    push cx

    mov ax, [current_segment]
    mov es, ax
    xor bx, bx
    mov ax, [current_sector]
    call read_sector_chs
    jc disk_error

    add word [current_segment], 0x20
    inc word [current_sector]

    pop cx
    loop .sector_loop

    ret

read_sector_chs:
    push ax
    push bx
    push cx
    push dx

    dec ax

    xor dx, dx
    mov bx, 18
    div bx
    mov cl, dl
    inc cl

    xor dx, dx
    mov bx, 2
    div bx
    mov ch, al
    mov dh, dl

    mov ah, 0x02
    mov al, 0x01
    xor bx, bx
    mov dl, [boot_drive]
    int 0x13
    jc .failed

    clc
    jmp .done

.failed:
    stc

.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

disk_error:
    mov ax, 0x0003
    int 0x10
    mov si, msg_disk_error
    call print_string_16

.halt:
    cli
    hlt
    jmp .halt

print_string_16:
    lodsb
    test al, al
    jz .done

    cmp al, 0x0A
    je .newline

    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_string_16

.newline:
    mov ah, 0x0E
    mov al, 0x0D
    int 0x10
    mov al, 0x0A
    int 0x10
    jmp print_string_16

.done:
    ret

boot_drive:             db 0
selected_mode:          dw 0
current_sector:         dw 0
current_segment:        dw 0

vbe_mode_candidates:
    dw 0x0160             ; VirtualBox custom mode 1: configured as 1920x1080x32
    dw 0x0161             ; VirtualBox custom mode 2: configured as 1680x1050x32
    dw 0x0162             ; VirtualBox custom mode 3: configured as 1440x900x32
    dw 0x0163             ; VirtualBox custom mode 4: configured as 1280x800x32
    dw 0x0145             ; VirtualBox/Bochs 1280x1024x32 when available
    dw 0x011B             ; VBE 1280x1024, often 24 bpp
    dw 0x0144             ; VirtualBox/Bochs 1024x768x32
    dw 0x0143             ; VirtualBox/Bochs 800x600x32
    dw 0x0142             ; VirtualBox/Bochs 640x480x32
    dw 0x0118             ; VBE 1024x768, often 24 bpp
    dw 0x0115             ; VBE 800x600, often 24 bpp
    dw 0

msg_stage2:              db "LiquidOS Stage 2: preparing the machine.", 0x0A, 0
msg_memmap:              db "Reading BIOS memory map...", 0x0A, 0
msg_video:               db "Selecting VBE graphics mode...", 0x0A, 0
msg_video_failed:        db "No framebuffer mode found; kernel will use fallback text.", 0x0A, 0
msg_kernel:              db "Loading kernel from boot image...", 0x0A, 0
msg_entering_pm:         db "Entering protected mode, then long mode.", 0x0A, 0
msg_disk_error:          db "Kernel disk read failed. Rebuild the image.", 0x0A, 0

align 8
gdt_start:
gdt_null:
    dq 0

gdt_code32:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_code64:
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10011010b
    db 00100000b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BITS 32

protected_mode_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Keep this temporary stack below the kernel staging area. The kernel is
    ; loaded at 0x10000 before being copied to 1 MiB.
    mov esp, 0x00006F00

    call copy_kernel_to_high_memory
    call build_identity_page_tables
    call enter_long_mode

.halt:
    hlt
    jmp .halt

copy_kernel_to_high_memory:
    cld
    mov esi, KERNEL_TEMP_ADDRESS
    mov edi, KERNEL_LOAD_ADDRESS
    mov ecx, (KERNEL_SECTOR_COUNT * SECTOR_SIZE) / 4
    rep movsd
    ret

build_identity_page_tables:
    cld

    mov edi, PAGE_PML4_ADDRESS
    mov ecx, (4096 * 6) / 4
    xor eax, eax
    rep stosd

    mov dword [PAGE_PML4_ADDRESS], PAGE_PDPT_ADDRESS | 0x003
    mov dword [PAGE_PML4_ADDRESS + 4], 0

    mov dword [PAGE_PDPT_ADDRESS + 0], PAGE_PD0_ADDRESS | 0x003
    mov dword [PAGE_PDPT_ADDRESS + 4], 0
    mov dword [PAGE_PDPT_ADDRESS + 8], PAGE_PD1_ADDRESS | 0x003
    mov dword [PAGE_PDPT_ADDRESS + 12], 0
    mov dword [PAGE_PDPT_ADDRESS + 16], PAGE_PD2_ADDRESS | 0x003
    mov dword [PAGE_PDPT_ADDRESS + 20], 0
    mov dword [PAGE_PDPT_ADDRESS + 24], PAGE_PD3_ADDRESS | 0x003
    mov dword [PAGE_PDPT_ADDRESS + 28], 0

    mov edi, PAGE_PD0_ADDRESS
    xor ebx, ebx
    mov ecx, 512 * 4

.map_loop:
    mov eax, ebx
    or eax, 0x083
    mov [edi], eax
    mov dword [edi + 4], 0

    add ebx, 0x00200000
    add edi, 8
    loop .map_loop

    ret

enter_long_mode:
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, PAGE_PML4_ADDRESS
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    jmp CODE64_SEG:long_mode_entry

BITS 64

long_mode_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, 0x000000000008F000
    mov rbp, 0
    mov rdi, BOOT_INFO_ADDRESS
    mov rax, KERNEL_LOAD_ADDRESS
    jmp rax
