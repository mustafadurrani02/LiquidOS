BITS 64

GLOBAL kernel_entry
GLOBAL kernel_stack_top
EXTERN kernel_main
EXTERN __bss_start
EXTERN __bss_end

SECTION .text.entry

kernel_entry:
    mov rbx, rdi

    ; The bootloader loads a flat binary. The ELF .bss section is not stored
    ; in that binary, so the kernel must clear it before using globals.
    cld
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    mov rsp, kernel_stack_top
    xor rbp, rbp

    ; Restore the BootInfo pointer from Stage 2.
    mov rdi, rbx
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

SECTION .bss
ALIGN 16
kernel_stack_bottom:
    resb 65536
kernel_stack_top:
