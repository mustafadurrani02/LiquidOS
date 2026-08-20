BITS 64

GLOBAL user_enter
GLOBAL user_resume_context
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
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
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

; void user_resume_context(u64 pml4_phys, const ProcessContext *context)
user_resume_context:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rel saved_kernel_rsp], rsp
    mov cr3, rdi

    mov ax, GDT_USER_DATA
    mov ds, ax
    mov es, ax

    mov rdx, rsi
    push qword [rdx + 152] ; ss
    push qword [rdx + 144] ; rsp
    push qword [rdx + 136] ; rflags
    push qword [rdx + 128] ; cs
    push qword [rdx + 120] ; rip

    mov r15, [rdx + 0]
    mov r14, [rdx + 8]
    mov r13, [rdx + 16]
    mov r12, [rdx + 24]
    mov r11, [rdx + 32]
    mov r10, [rdx + 40]
    mov r9,  [rdx + 48]
    mov r8,  [rdx + 56]
    mov rbp, [rdx + 80]
    mov rcx, [rdx + 96]
    mov rbx, [rdx + 104]
    mov rdi, [rdx + 72]
    mov rax, [rdx + 112]
    mov rsi, [rdx + 64]
    mov rdx, [rdx + 88]
    iretq

user_return_to_kernel_now:
    cli
    mov ax, GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, [rel saved_kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    sti
    ret
