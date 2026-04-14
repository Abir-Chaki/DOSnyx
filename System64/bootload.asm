BITS 32

section .multiboot2
align 8
    dd 0xE85250D6
    dd 0
    dd header_end - header_start
    dd -(0xE85250D6 + 0 + (header_end - header_start))
header_start:
    dw 0
    dw 0
    dd 8
header_end:

section .bss
align 4096
pml4_table:  resb 4096
pdpt_table:  resb 4096
pd_table:    resb 4096

section .text
global _start
extern kernel_main

_start:
    cli

    ; Identity map first 2MB (huge page)

    mov eax, pd_table
    or eax, 0b11
    mov [pdpt_table], eax

    mov eax, pdpt_table
    or eax, 0b11
    mov [pml4_table], eax

    mov ecx, 0
.map:
    mov eax, ecx
    shl eax, 21                 ; Multiply index by 2MB
    or eax, 0b10000011          ; Present + Writeable + Huge Page
    mov [pd_table + ecx*8], eax ; Write entry (8 bytes per entry in 64-bit)
    
    inc ecx
    cmp ecx, 512                ; Map 512 entries (1GB total)
    jne .map

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Load PML4
    mov eax, pml4_table
    mov cr3, eax

    ; Enable Long Mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64.pointer]
    jmp 0x08:long_mode_start

; =====================================
; 64-bit mode
; =====================================

BITS 64

long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    call kernel_main

.hang:
    hlt
    jmp .hang

; =====================================
; GDT
; =====================================

section .rodata
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF

.pointer:
    dw $ - gdt64 - 1
    dq gdt64

; =====================================
; INTERRUPT SYSTEM
; =====================================

section .text

extern isr_dispatch

isr_common:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    mov rdi, rsp
    call isr_dispatch

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    add rsp, 16
    iretq

%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
    jmp isr_common
%endmacro

global load_idt
load_idt:
    lidt [rdi]
    ret

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; =====================================
; IRQ0 TIMER
; =====================================

global irq0_handler
extern isr_timer

irq0_handler:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    call isr_timer

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    iretq

; =====================================
; IRQ1 KEYBOARD
; =====================================

global irq1_handler
extern isr_keyboard

irq1_handler:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    call isr_keyboard

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    iretq