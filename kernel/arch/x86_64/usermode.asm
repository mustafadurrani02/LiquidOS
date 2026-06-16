BITS 64

GLOBAL user_enter
GLOBAL user_return_to_kernel_now

%define GDT_KERNEL_DATA 0x10
%define GDT_USER_DATA   0x1B
%define GDT_USER_CODE   0x23

SECTION .bss
ALIGN 8
saved_kernel_rsp:
    resq 1

SECTION .text

; void user_enter(u64 pml4_phys, u64 entry_rip, u64 user_rsp)
user_enter:
    mov [rel saved_kernel_rsp], rsp
    mov cr3, rdi

    mov ax, GDT_USER_DATA
    mov ds, ax
    mov es, ax

    push qword GDT_USER_DATA
    push rdx
    push qword 0x202
    push qword GDT_USER_CODE
    push rsi
    iretq

user_return_to_kernel_now:
    cli
    mov ax, GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, [rel saved_kernel_rsp]
    sti
    ret
